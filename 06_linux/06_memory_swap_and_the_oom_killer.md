# LX 06: Memory, swap and the OOM killer in practice

*Part of the [Linux plan](00_plan.md) · Time: about 5 hours · You need: [LX 03](03_processes_signals_and_proc.md) (`/proc`, process states), [LX 04](04_boot_systemd_and_logs.md) (cgroups, `dmesg`), [LX 05](05_performance_methodology_and_triage.md) (read `available`, not `free`; memory saturation and pressure) and [OS 11](../05_operating_systems/11_virtual_memory.md) if you want the page-table and paging theory under the numbers. The earlier tutorials said "memory is full" in one line. This one says **which memory**: what each line of `/proc/meminfo` counts, what a process really costs, when the kernel says yes to a request, when it swaps, who it kills and how to read the log.*

"A process was killed and nobody ran anything" and "`free` says 200 MB, is the box dead?" are two of the most common Linux questions in systems interviews, and both have a precise answer. The imprecise answer is "the kernel ran out of memory". The precise one names the **kind** of memory (anonymous, page cache, shared, slab, huge), the **limit** that was hit (the machine, or a cgroup) and the **rule** that picked the victim. This tutorial builds that vocabulary in order: every field of `/proc/meminfo` with a measured example (section 1), the four sizes of a process and why they disagree (2), overcommit (3), swap and `swappiness` (4), the OOM killer and its log (5), the `vm.*` sysctls and why `drop_caches` is not a fix (6), huge pages and NUMA (7), cgroup memory limits and memory pressure (8), finding a leak (9), then costs, mistakes, questions and five problems.

**Seven honest limits, stated first.**

1. Everything is measured on **one Linux 6.18 VM** (Firecracker, 2 vCPUs, `MemTotal` 8,216,668 kB), as **root**, with procps-ng 4.0.4, `smem`, `pmap`, `numactl`, `slabtop` and gcc 13. The VM restarted between LX 05 and this tutorial, so it is a fresh machine with a small page cache.
2. A **global OOM kill was deliberately not triggered**: on this sandbox it could kill the process that runs the session. Every kill shown here happens **inside a v1 memory cgroup**, which is the same code path with a different scope (section 5 says what differs).
3. The control groups are **version 1**, and the memory controller shares its hierarchy with the sandbox's own groups, so it could not be released to version 2. **The v2 files (`memory.max`, `memory.high`, `memory.events`, `memory.pressure`) are therefore described from the kernel documentation and marked "from memory", and the v1 equivalents are the ones measured.**
4. **There is one NUMA node**, so `numactl` and `numastat` show a degenerate case; cross-node behaviour is explained, not measured. `numastat -m` reported node 0 with 4,698 MB while `/proc/meminfo` says 8,024 MB; I did not chase the difference, so do not read the node file as the whole machine.
5. The sandbox lacks `CAP_SYS_RESOURCE`, so **`oom_score_adj` can be raised but not lowered** (writing -500 failed with `Permission denied`). That means the immunity value **-1000 is described, not measured**. Also, `/proc/PID/oom_score` here read **666 for a 3.5 MB shell and 692 for a 300 MB process**, so the number is **not usable as a size ranking in this VM**; the formula in section 5 is from the kernel documentation and the order of kills is what I measured.
6. `MemFree` moves by tens of megabytes between runs because other things allocate (the sandbox agents, the page cache). In the section 1 table, **the fields that are exact are the ones I name**; treat `MemFree` rows as about plus or minus 100 MB.
7. `perf`, `bpftrace` and `tcpdump` are not available, so allocation tracing (who calls `malloc`) is left to [LX 10](00_plan.md) and [LX 11](00_plan.md); statements about them are from memory.

By the end you can:

- read `/proc/meminfo` and say which lines overlap (measured: 100 MB written to a tmpfs file moved **`Shmem` +100 and `Cached` +101**, so `Cached` includes shared memory and tmpfs, which `drop_caches` cannot release);
- explain **`MemAvailable`** and why it beats `MemFree` (measured: reading a 200 MB file took `MemFree` down by **199 MB** and `MemAvailable` by **0**, because clean page cache is reclaimable);
- tell **VSZ, RSS, PSS and USS** apart (measured: `malloc` of 1 GiB took VSZ from 2,724 kB to **1,051,304 kB** and RSS from 1,428 kB to 1,748 kB; touching 10 MiB took RSS to **11,984 kB**) and say why PSS is the number to add up (measured: a parent and child each showed RSS of about 110 MB, PSS of **58 and 53 MB**, USS of **5 and 1 MB** right after `fork`);
- state the three **overcommit modes** and verify `CommitLimit` (measured: `(8,216,668 kB × 50%) + 0 swap = 4,108,334 kB` against **4,108,332 kB** reported; asking for 30 GiB was refused in mode 0, granted in mode 1; mode 2 refused 6 GiB);
- describe what **swap** does and does not do (measured: a 180 MB array in a 120 MB cgroup swapped out **89 MB** while it was being touched, and a second pass over the array swapped **180 MB** back in and took **0.60 s** against 0.32 s for the first touch);
- read an **OOM kill line** field by field and say what picks the victim (measured: with equal `oom_score_adj`, the process holding **69 MB** (`anon-rss` 69,120 kB) was killed rather than the one holding 40 MB; with `oom_score_adj` 1000 on the 40 MB process, **it** was killed, with exit status **137**);
- say what **`drop_caches`** does and costs (measured: 351 MB of page cache dropped in one write, 100 MB of tmpfs and 100 MB of shared memory stayed, and a cold re-read of a 300 MB file took 0.19 s against 0.05 s warm on this very fast disk);
- use **THP and hugetlb** (measured: `MADV_HUGEPAGE` on a 64 MiB region gave **65,536 kB of `AnonHugePages`**; `MAP_HUGETLB` failed with `Cannot allocate memory` until 40 pages were reserved);
- set and read **cgroup memory limits** (v1 measured: limit, `failcnt`, `oom_control`; v2 files from memory) and find a **leak** (measured: `Pss_Anon` grew by about **4 MB per second**, one anonymous mapping grew 12,288 kB in 3 s, and 100,000 empty files made the kernel hold **100,254 `ext4_inode_cache` and 101,619 `dentry` objects**).

**Where the numbers come from.** Eight experiment scripts (`x1` to `x8`) plus a problem-answer script were run in this session on Linux 6.18.44. Transcripts print `$ command` before each command's output. Numbers that are not from these runs are marked "from memory" or come from a source listed at the end.

**Data and timing.** Nothing here needs a data set. The load programs are small C and Python programs that allocate, touch, fork and leak; the important ones are printed where they are used. Each takes seconds, except the leak-to-kill timing in Problem E (about 45 s). Treat all timings as ranges.

---

## 1. What `/proc/meminfo` counts

`free` is a summary of `/proc/meminfo`, and `/proc/meminfo` is a list of **counters that overlap**. The first skill is to know which lines are parts of which.

```text
--- the fields, once (kB), then the same as free reports them
$ grep -E "^(MemTotal|MemFree|MemAvailable|Buffers|Cached|SwapTotal|AnonPages|Mapped|Shmem|Slab|SReclaimable|SUnreclaim|KernelStack|PageTables|CommitLimit|Committed_AS|AnonHugePages|HugePages_Total|Hugepagesize)" /proc/meminfo
MemTotal:        8216668 kB
MemFree:         7572440 kB
MemAvailable:    7576468 kB
Buffers:           34640 kB
Cached:           193376 kB
SwapTotal:             0 kB
AnonPages:        344384 kB
Mapped:           169012 kB
Shmem:             16636 kB
Slab:              24364 kB
SReclaimable:       4748 kB
SUnreclaim:        19616 kB
KernelStack:        1700 kB
PageTables:         3924 kB
CommitLimit:     4108332 kB
Committed_AS:     599300 kB
AnonHugePages:         0 kB
ShmemHugePages:        0 kB
ShmemPmdMapped:        0 kB
HugePages_Total:       0
Hugepagesize:       2048 kB
$ free -m
               total        used        free      shared  buff/cache   available
Mem:            8024         625        7394          16         227        7398
Swap:              0           0           0
```

How to read it, top down (the fields come from the kernel's `proc(5)` documentation and the memory-management admin guide, plus what is measured below):

| Field | What it counts | Notes |
| --- | --- | --- |
| `MemTotal` | RAM the kernel manages | less than installed: the kernel image and reserved areas are excluded |
| `MemFree` | pages on the free lists, doing nothing | **small is normal**: unused RAM is wasted RAM |
| `MemAvailable` | an **estimate** of what could be given to a new process without swapping | free pages plus reclaimable page cache plus reclaimable slab, less the kernel's reserves; **the number to read** |
| `Buffers` | page cache of block devices (raw device reads, filesystem metadata) | usually small |
| `Cached` | page cache of files, **including tmpfs and shared anonymous memory (`Shmem`)** | the overlap that surprises people |
| `AnonPages` | anonymous memory mapped into processes: the heap, the stack, `mmap(MAP_ANONYMOUS)` private | the closest thing to "memory the programs use" |
| `Mapped` | file pages currently mapped into a process (shared libraries, `mmap` of files) | a subset of `Cached` |
| `Shmem` | tmpfs, `/dev/shm`, `memfd` and `MAP_SHARED\|MAP_ANONYMOUS` memory | counted in `Cached` too; **cannot be dropped**, it can only be swapped |
| `Slab`, `SReclaimable`, `SUnreclaim` | kernel object caches: dentries, inodes, network buffers | the reclaimable part is dentries and inodes; the rest stays |
| `KernelStack`, `PageTables` | per-thread kernel stacks and the page tables themselves | grow with threads and with sparse mappings |
| `Dirty`, `Writeback` | file pages changed in memory and not yet written, and those being written now | section 6 |
| `Active(file)`, `Inactive(file)`, `Active(anon)`, `Inactive(anon)` | the LRU lists that reclaim scans | inactive is what goes first |
| `CommitLimit`, `Committed_AS` | what the kernel may promise, and what it has promised | section 3 |
| `AnonHugePages`, `Hugetlb`, `HugePages_*` | transparent and reserved huge pages | section 7 |

**What does each kind of allocation do to the counters?** `prog/memtool.py` (not printed; 48 lines) performs one allocation per step and prints the change in each field after the step, in MB; a `.` means under 0.5 MB.

```text
--- changes (MB) after each step; "." means under 0.5 MB
step                                MemFree MemAvail  Buffers   Cached AnonPage   Mapped    Shmem     Slab SReclaim PageTabl Committe Active(f Inactive    Dirty
(baseline, absolute MB not shown)       +55      +12      -34      -11        .        .        .       -1       -1       -1        .      -39       -6        .
touch 200 MB of heap (anonymous)       -205     -204        .        .     +200        .        .        .        .        .     +200        .        .        .
read the 200 MB file (page cache)      -199        .       +1     +200       +1        .        .        .        .        .       +1       +1     +200        .
write 100 MB into a tmpfs file         -103      -99       +3     +101        .        .     +100        .        .        .     +100       +3       +1        .
touch 100 MB MAP_SHARED|MAP_ANONYMOUS     -102     -102        .     +100        .     +100     +100        .        .        .     +100        .        .        .
write 150 MB to a file, no fsync       -270     -118        .     +150        .        .        .       +4       +4        .        .        .     +150     +150
fsync it (Dirty goes back down)           .        .        .        .        .        .        .        .        .        .        .        .        .     -150
echo 3 > drop_caches                   +486     +131       -4     -351        .        .        .       -4       -4        .        .       -4     -351        .
free the heap and the shared map       +217     +217        .     -100     -200     -100     -100        .        .       -1     -300        .        .        .
umount tmpfs, delete the files          +92      +95       +3      -99       -2        .     -100        .        .        .     -100       +3       +1        .
```

Read the rows one at a time, because each is a rule worth remembering:

- **Touching 200 MB of heap:** `AnonPages` +200, `Committed_AS` +200, `MemFree` and `MemAvailable` both down about 200. Anonymous memory is the one kind that is **not reclaimable without swap**, so `MemAvailable` falls with `MemFree`.
- **Reading a 200 MB file:** `Cached` +200 and `Inactive(file)` +200, `MemFree` down 199, **`MemAvailable` unchanged (shown as `.`)**. The pages are clean and can be dropped, so the kernel counts them as available. This is the whole reason to read `available`.
- **Writing 100 MB to a tmpfs file:** `Shmem` +100 **and** `Cached` +101, and `MemAvailable` falls by 99. A tmpfs page is cache with **no file behind it**, so it cannot be dropped, and the estimate correctly does not count it as available.
- **Touching 100 MB of `MAP_SHARED|MAP_ANONYMOUS`:** `Shmem` +100, `Mapped` +100 and `Cached` +100 together. The same page appears in three lines. It is **not** in `AnonPages`, which is why a database using shared memory can look small in `AnonPages` and large in `Shmem`.
- **Writing 150 MB without `fsync`:** `Cached` +150, `Dirty` +150, `Inactive(file)` +150. `MemFree` fell 270 and `MemAvailable` 118 in this row, a gap I did not explain (limit 6: these two are noisy at this scale), so trust the `Cached`, `Dirty` and `Inactive(file)` columns. After `fsync`, `Dirty` went back down by 150 and `Cached` stayed: the data is still cached, now clean.
- **`echo 3 > drop_caches`:** `Cached` -351, `Inactive(file)` -351, `Slab` -4. It removed the clean file pages and the small reclaimable slab, and **left the 100 MB of tmpfs and the 100 MB of shared memory**, because `Shmem` is not droppable.
- **Freeing the heap and the shared map:** `AnonPages` -200, `Mapped` -100, `Shmem` -100, `Cached` -100, `Committed_AS` -300. The accounting is symmetric.

**`free -m`, the same data.** `used` is `total - free - buff/cache` in current procps (it moved away from the older definition that left cache out), `shared` is `Shmem`, `buff/cache` is `Buffers + Cached + SReclaimable`, and `available` is `MemAvailable`. In the dump above, `free` printed 7,394 MB free and 227 MB `buff/cache` on a machine that has not run much, so almost everything is free; the point is that after a few file reads the `free` column shrinks and `available` does not.

**How `MemAvailable` is computed (from the kernel source, from memory).** Roughly: free pages minus the kernel's reserve (the sum of the zone **low watermarks**), plus the page cache that can be dropped (the active and inactive file lists, minus the smaller of half the cache and the low watermark), plus the reclaimable slab (with the same halving rule). It is an **estimate**, not a promise: in this VM the sum `MemFree + Buffers + Cached + SReclaimable - Shmem` was 7,605 MB while `MemAvailable` was 7,398 MB, a 207 MB gap that is the reserve and the halving.

---

## 2. A process has four sizes: VSZ, RSS, PSS and USS

`ps` and `top` show a size per process, and adding them up gives nonsense. There are four different quantities, and they answer different questions.

| Size | Meaning | Where to read it |
| --- | --- | --- |
| **VSZ** (`VmSize`) | address space **mapped**, touched or not | `ps -o vsz`, `/proc/PID/status` |
| **RSS** (`VmRSS`) | pages **resident in RAM** now, including shared ones in full | `ps -o rss`, `status`, `smaps_rollup` |
| **PSS** | resident pages with each shared page **divided by the number of processes sharing it** | `smaps_rollup`, `smem` |
| **USS** | pages **only this process** has; what freeing the process would give back | `smem`, `Private_Clean + Private_Dirty` |

(`Rss`, `Pss`, `Shared_Clean`, `Shared_Dirty`, `Private_Clean` and `Private_Dirty` are the fields defined in `proc_pid_smaps(5)`; `Pss_Anon` and `Pss_File` split PSS into anonymous and file-backed memory, and `smaps_rollup` is the sum of `smaps` for the whole process, which is much cheaper to read on a big process.)

### 2.1 Malloc is a promise, touching is the cost

```c
/* lazy.c: ask for 1 GiB, touch 10 MiB: virtual size and resident size are different numbers */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void sizes(const char *label) {
    long vsz = 0, rss = 0;
    FILE *f = fopen("/proc/self/status", "r");
    char line[256];
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "VmSize:", 7)) vsz = atol(line + 7);
        if (!strncmp(line, "VmRSS:", 6)) rss = atol(line + 6);
    }
    fclose(f);
    printf("%-26s VSZ %8ld kB   RSS %7ld kB\n", label, vsz, rss);
}

int main(void) {
    sizes("start");
    volatile char *p = malloc(1UL << 30);
    sizes("after malloc(1 GiB)");
    for (unsigned long i = 0; i < (10UL << 20); i += 4096) p[i] = 1;
    sizes("after touching 10 MiB");
    return p[0] == 1 ? 0 : 1;
}
```

```text
--- virtual against resident: malloc is a promise, touching is the cost
start                      VSZ     2724 kB   RSS    1428 kB
after malloc(1 GiB)        VSZ  1051304 kB   RSS    1748 kB
after touching 10 MiB      VSZ  1051304 kB   RSS   11984 kB
```

`malloc(1 GiB)` raised **VSZ by about 1 GiB** (2,724 to 1,051,304 kB) and **RSS by 320 kB**: the allocator mapped the range and the kernel handed out nothing yet. Touching one byte per 4 KiB page for 10 MiB raised RSS to **11,984 kB**, about 10 MiB more. This is **demand paging**: a page is allocated on the **first access**, by a page fault ([OS 11](../05_operating_systems/11_virtual_memory.md)). Consequences you will meet:

- **VSZ is nearly useless for sizing.** A JVM or a Go runtime reserves tens of GiB of address space and uses a few hundred MB.
- **RSS grows when you touch, not when you allocate.** A service that allocates a 10 GiB pool at start and fills it slowly looks small, then looks large, and can be OOM-killed hours later, on the page that finally crosses the line.
- `volatile` and the touch loop are in the program because **the compiler removes an unused `malloc`**: my first version printed the same VSZ before and after, because at `-O1` the allocation was optimised away. Benchmarks and demos of memory need the memory to be **used** ([LX 05](05_performance_methodology_and_triage.md) section 7.2 made the same point for CPU loops).

### 2.2 Fork, copy-on-write, and why you cannot add up RSS

```python
# share.py: VSZ, RSS, PSS and USS of a parent and a forked child, before and after the child writes
import os, sys, time

MB = 1 << 20
def rollup(pid):
    d = {}
    for line in open("/proc/%d/smaps_rollup" % pid):
        p = line.split()
        if len(p) == 3 and p[2] == "kB": d[p[0].rstrip(":")] = int(p[1]) / 1024.0
    return d
def vsz(pid):
    return int([l for l in open("/proc/%d/status" % pid) if l.startswith("VmSize")][0].split()[1]) / 1024.0
def row(label, pid):
    r = rollup(pid)
    uss = r["Private_Clean"] + r["Private_Dirty"]
    print("%-34s VSZ %7.0f  RSS %6.0f  PSS %6.0f  USS %6.0f  shared %6.0f" % (label, vsz(pid), r["Rss"], r["Pss"], uss, r["Shared_Clean"] + r["Shared_Dirty"]))

lazy = bytearray(0)
big = bytearray(100 * MB)
for i in range(0, len(big), 4096): big[i] = 1
row("parent, 100 MB touched", os.getpid())
r_, w_ = os.pipe(); r2, w2 = os.pipe()
pid = os.fork()
if pid == 0:
    os.read(r_, 1)
    for i in range(0, 50 * MB, 4096): big[i] = 2          # the child writes to half of it
    os.write(w2, b"x"); os.read(r_, 1); os._exit(0)
time.sleep(0.5)
row("parent, after fork", os.getpid()); row("child, after fork (nothing written)", pid)
os.write(w_, b"g"); os.read(r2, 1)
row("parent, child wrote 50 MB", os.getpid()); row("child, wrote 50 MB", pid)
os.write(w_, b"q"); os.waitpid(pid, 0)
```

```text
--- fork: the child shares the parent's pages until it writes (copy on write)
parent, 100 MB touched             VSZ     135  RSS    111  PSS    110  USS    109  shared      2
parent, after fork                 VSZ     135  RSS    112  PSS     58  USS      5  shared    107
child, after fork (nothing written) VSZ     135  RSS    106  PSS     53  USS      1  shared    105
parent, child wrote 50 MB          VSZ     135  RSS    112  PSS     82  USS     55  shared     57
child, wrote 50 MB                 VSZ     135  RSS    106  PSS     78  USS     51  shared     55
```

Read the five lines in order.

1. **Before the fork:** the parent has touched 100 MB; RSS 111, PSS 110, USS 109. One process, so all three nearly agree (the 2 MB shared are the interpreter's libraries).
2. **After `fork`, nothing written:** the parent's RSS is still 112 and the child's is 106, **so `ps` shows two processes of about 110 MB**, but **PSS is 58 and 53** and **USS is 5 and 1**. Almost everything is **shared** (107 and 105 MB) between the two. `fork` copied the page tables and marked the pages read-only; the pages are one physical copy.
3. **After the child writes 50 MB:** each write faults, and the kernel **copies the page** for the writer (copy-on-write). Now the child's USS is **51** and the parent's is **55** (the parent's 50 MB became private too, because with the child's copy made, the parent's page has one owner again), PSS is 82 and 78, and shared fell to 57 and 55. The pages the child did not write are still shared.

The lesson for sizing a forking server (a pre-fork web server, `fork`-based workers, a Redis `BGSAVE` child) is: **add up PSS, never RSS**, and expect each worker's **USS** to grow as it writes. Problem B measures it with four workers: the sum of RSS was **535 MB**, the sum of PSS **151 MB**, and the machine's used memory grew by **55 MB** after the fork (the 100 MB array was already counted before).

### 2.3 The tools on one process

```text
--- the same numbers from the tools: smem (PSS and USS) and pmap for one process
$ smem -c "pid pss uss rss" 2>&1 | awk "NR==1 || \$1==$p"
  PID      PSS      USS      RSS
 1168    50347    50032    52220
$ pmap -x $p | tail -12 | cut -c1-90
00007f32a083f000      16       0       0 r----   [ anon ]
00007f32a0843000       8       0       0 r----   [ anon ]
00007f32a0845000       8       4       0 r-x--   [ anon ]
00007f32a0847000       4       4       0 r---- ld-linux-x86-64.so.2
00007f32a0848000     172     172       0 r-x-- ld-linux-x86-64.so.2
00007f32a0873000      40      40       0 r---- ld-linux-x86-64.so.2
00007f32a087d000       8       8       8 r---- ld-linux-x86-64.so.2
00007f32a087f000       8       8       8 rw--- ld-linux-x86-64.so.2
00007ffd13be8000     164     116     116 rw---   [ stack ]
ffffffffff600000       4       0       0 --x--   [ anon ]
---------------- ------- ------- -------
total kB           76560   52220   44232
$ grep -E "^(Rss|Pss|Pss_Anon|Pss_File|Private_Dirty|Shared_Clean|Swap)" /proc/$p/smaps_rollup
Rss:               52220 kB
Pss:               50496 kB
Pss_Dirty:         44232 kB
Pss_Anon:          44232 kB
Pss_File:           6264 kB
Pss_Shmem:             0 kB
Shared_Clean:       2064 kB
Private_Dirty:     44232 kB
Swap:                  0 kB
SwapPss:               0 kB
```

`smem` printed PSS **50,347**, USS **50,032** and RSS **52,220** kB for a Python process holding a 40 MiB array: USS and PSS agree because nothing is shared, and RSS exceeds both by the shared interpreter and library pages. `pmap -x` lists every mapping with its size, RSS and dirty columns, and the last line (`total kB 76560 52220 44232`) is the VSZ, RSS and dirty total. `smaps_rollup` gives the split: **`Pss_Anon` 44,232 kB** (the heap), **`Pss_File` 6,264 kB** (libraries and the executable), `Private_Dirty` 44,232 kB, `Swap` 0.

**Which one goes in a capacity plan?** PSS summed over the processes for steady state, and the **USS of one worker** for "how much does one more worker cost". RSS for the largest single process only when you ask "can it fit". VSZ never, except to watch an address-space limit (`ulimit -v`, 32-bit processes, `vm.max_map_count`).

---

## 3. Overcommit: when the kernel says yes

A successful `malloc` does not mean the memory exists (section 2.1). The kernel decides **at `mmap` or `brk` time** whether to accept the request, according to `vm.overcommit_memory`. The accepted total is tracked in **`Committed_AS`** and, in mode 2, compared with **`CommitLimit`**.

| Mode | Name | Rule (kernel documentation) |
| --- | --- | --- |
| 0 | heuristic (default) | "obvious overcommits of address space are refused": a single request larger than the RAM plus swap the kernel could possibly give is refused |
| 1 | always | every request succeeds; the first page fault that finds no memory goes to the OOM killer |
| 2 | never | the total committed address space may not exceed **swap plus `overcommit_ratio` percent of RAM** (default 50), or an absolute `overcommit_kbytes` |

```c
/* oc.c: try to reserve N GiB (no touching) and report whether the kernel said yes */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>

int main(int argc, char **argv) {
    double gib = atof(argv[1]);
    size_t n = (size_t)(gib * (1UL << 30));
    void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) printf("  ask %5.1f GiB -> refused (%s)\n", gib, strerror(errno));
    else printf("  ask %5.1f GiB -> granted\n", gib);
    return 0;
}
```

```text
$ grep -E "^(MemTotal|SwapTotal|CommitLimit|Committed_AS)" /proc/meminfo
MemTotal:        8216668 kB
SwapTotal:             0 kB
CommitLimit:     4108332 kB
Committed_AS:     598660 kB
$ sysctl vm.overcommit_memory vm.overcommit_ratio
vm.overcommit_memory = 0
vm.overcommit_ratio = 50

--- vm.overcommit_memory = 0
  ask   1.0 GiB -> granted
  ask   6.0 GiB -> granted
  ask  30.0 GiB -> refused (Cannot allocate memory)

--- vm.overcommit_memory = 1
  ask   1.0 GiB -> granted
  ask   6.0 GiB -> granted
  ask  30.0 GiB -> granted

--- vm.overcommit_memory = 2
  ask   1.0 GiB -> granted
  ask   6.0 GiB -> refused (Cannot allocate memory)
  ask  30.0 GiB -> refused (Cannot allocate memory)

--- mode 2 with a bigger ratio: vm.overcommit_ratio = 90
CommitLimit:     7395000 kB
  ask   1.0 GiB -> granted
  ask   6.0 GiB -> granted
  ask  30.0 GiB -> refused (Cannot allocate memory)

$ sysctl vm.overcommit_memory vm.overcommit_ratio
vm.overcommit_memory = 0
vm.overcommit_ratio = 50
```

- **The formula checks out.** `CommitLimit` reads **4,108,332 kB**; `MemTotal × 50% + SwapTotal` is 8,216,668 × 0.5 + 0 = 4,108,334. (The kernel subtracts reserved huge pages first, and there were none; Problem D repeats the check and then adds swap.)
- **Mode 0:** 1 GiB and **6 GiB were granted** (more than half of RAM, so a mode 2 limit would have refused it) and **30 GiB was refused**. Mode 0 is permissive: it only stops the request that could never be satisfied.
- **Mode 1:** all three granted, including 30 GiB on an 8 GiB machine. Nothing is allocated; the bill arrives at first touch.
- **Mode 2 at ratio 50:** 1 GiB granted (`Committed_AS` was 598,660 kB, so the sum was well under 4,108,332), **6 GiB refused** with `Cannot allocate memory`. With `overcommit_ratio` 90 the limit became **7,395,000 kB** and 6 GiB was granted again; 30 GiB stayed refused.

**What to choose, and the trade-off.** Mode 0 is right for almost everything: programs reserve far more than they touch (a fork of a 10 GiB process is fine because the pages are shared copy-on-write). Mode 1 suits workloads with huge sparse arrays that would be refused otherwise (scientific code, some in-memory databases; the kernel documentation names sparse arrays). Mode 2 turns a **delayed, random kill** into an **immediate, local `ENOMEM`** at the `malloc` that asked, which is a better failure for a system that must not lose processes, **but** it makes the machine refuse work while most RAM is still idle, and programs that assume `malloc` never fails (or that `fork` of a big process is free) break. Redis, for one, documents in its own warning that it wants `vm.overcommit_memory = 1` so that `fork` for a snapshot cannot be refused (from memory).

**`Committed_AS` against `CommitLimit` is a number to chart**, even in mode 0, because it tells you how far the promises have run ahead of the memory: here 598,660 kB of promises against 8,216,668 kB of RAM is a comfortable machine, and on a busy host a `Committed_AS` several times `MemTotal` means the next burst of page faults has a real chance of finding nothing.

---

## 4. Swap and `swappiness`

**What swap is for.** Swap gives the kernel somewhere to put **anonymous pages**, which have no file to go back to. Clean file pages never need swap (they are dropped and re-read), so a machine with no swap can still reclaim all its page cache; what it cannot do is move cold heap out of the way. Swap is therefore about **cold anonymous memory**, not about "extra RAM". A **swap file** works the same way as a partition; the experiment used a 300 MB file with `mkswap` and `swapon`, which the kernel listed at priority -2.

**How the kernel chooses what to evict.** When free memory falls below a watermark (section 6), reclaim scans the LRU lists ([LX 05](05_performance_methodology_and_triage.md) section 4.4): anonymous and file pages, inactive first. **`vm.swappiness`** (0 to 200, default 60) sets the **relative cost** of reclaiming anonymous pages (writing them to swap) against file pages (dropping or writing them back). 100 means the two are treated as equal cost; lower makes the kernel prefer dropping file pages; higher (up to 200) suits cheap swap such as zram. At 0 the kernel swaps only when free memory plus file pages is below the high watermark (kernel documentation). Each memory cgroup can override it with `memory.swappiness` (v1).

### 4.1 An array larger than its cgroup

A v1 cgroup of **120 MB** with a **memory plus swap limit of 400 MB**, a 300 MB swap file, `swappiness` 60, and a Python process that touches a **180 MB array**, then reads it all again:

```text
--- 1. array bigger than the limit: 180 MB of anon in a 120 MB cgroup, swappiness 60
  first touch of 180 MB: 0.32 s
  swapped out while touching: 89 MB, while reading the file: 0 MB, VmSwap now 89 MB
  second pass over the array: 0.60 s, swapped in 180 MB
  cgroup usage 119 MB  limit 120 MB  failcnt 110495
```

The first pass **swapped out 89 MB** of the array while it was being touched (63 MB in an earlier run: expect a range) and took **0.32 s**: the array exceeds the group by 60 MB and the cgroup hit its limit **110,495 times** (`failcnt`). The **second pass**, which only reads, took **0.60 s and swapped in 180 MB**, more than the 89 MB that had gone out: each page read from swap pushed another page out, and the pass paged the same data in and out as it walked through it. That is **thrashing** in miniature, measured: a working set (180 MB) larger than the memory (120 MB) cannot be fixed by swap, only made slower by it. The cost of one swap-in is the number to remember: here about **13 µs per 4 KiB page** (0.60 s over about 46,000 pages, each page-in also pushing a page out), on a very fast virtual disk; on a real SSD it is tens of microseconds, on a spinning disk milliseconds (from memory).

### 4.2 Swappiness 0 against 100: cold file pages go first

An **80 MB array** and a **200 MB file** read through the same 120 MB cgroup, once at `swappiness` 0 and once at 100:

```text
--- 2. 80 MB array plus a 200 MB file read, in a 120 MB cgroup: does the kernel swap the array or drop the file?
swappiness = 0
  first touch of 80 MB: 0.00 s
  swapped out while touching: 0 MB, while reading the file: 0 MB, VmSwap now 0 MB
  second pass over the array: 0.00 s, swapped in 0 MB
  cgroup usage 119 MB  limit 120 MB  failcnt 186
swappiness = 100
  first touch of 80 MB: 0.00 s
  swapped out while touching: 0 MB, while reading the file: 0 MB, VmSwap now 0 MB
  second pass over the array: 0.00 s, swapped in 0 MB
  cgroup usage 119 MB  limit 120 MB  failcnt 168
```

At **both** settings the array **stayed in RAM** (swapped out 0, `VmSwap` 0) and the kernel dropped file pages instead (the cgroup sat at 119 MB with a `failcnt` of 186 and 168). The file was read once, so its pages were on the **inactive** list, the cheapest thing to reclaim; with a 80 MB array that was in use, there was nothing anonymous worth evicting at either setting. So **`swappiness` is a bias, not a switch**: it decides what the kernel does when **both** kinds are candidates. To see swap at 100 you need the file to be **hot** (read repeatedly, so its pages are active); I did not build that case, and the statement that `swappiness` 100 starts swapping earlier under a mixed load is from the documentation, not from this run.

**What to do on servers (from experience and the documentation, not measured here).**

- A machine with **no swap** cannot evict cold heap, so a memory spike goes straight to the OOM killer; a **small swap** (a few percent of RAM) buys time and shows pressure in `si` and `so` before the kill.
- **Swap is not a performance setting**: what you want is the **rate** of swapping to be near zero. A used swap with `si` and `so` at 0 is cold data parked, and is fine; a **non-zero `si` and `so` sustained** is thrashing ([LX 05](05_performance_methodology_and_triage.md) section 4.4, `vmstat 1`).
- Databases (PostgreSQL, MySQL) are often run with `swappiness` 1 to 10 so that the buffer pool is not swapped for the page cache; Kubernetes historically required swap off (from memory; recent versions allow it with a setting).
- `zram` and `zswap` compress pages in RAM before or instead of writing them out; they change the cost model that `swappiness` encodes.

---

## 5. The OOM killer

### 5.1 When it runs

The OOM killer runs when the kernel **cannot reclaim enough memory to satisfy an allocation** and the allocation is allowed to kill: all file pages that can be dropped are gone, anonymous pages cannot be swapped (no swap, or swap full), and the cgroup or the machine is out. Two scopes exist:

- **Global OOM:** the whole machine is out. The log says `Out of memory: Killed process ...` and the constraint is `CONSTRAINT_NONE`. (From memory, since I did not trigger one; the format is in `mm/oom_kill.c`.)
- **Cgroup OOM:** a memory cgroup reached its limit and could not reclaim inside it. The kill is **confined to the cgroup**; the rest of the machine is untouched. The log says `Memory cgroup out of memory: Killed process ...` and the constraint is `CONSTRAINT_MEMCG`. This is the case measured below, and it is the one you meet in containers: a container with a memory limit dies while the host has gigabytes free.

Two sysctls change the global behaviour: **`vm.oom_kill_allocating_task`** (default 0: pick by badness; non-zero: kill the process that triggered it, skipping the scan) and **`vm.panic_on_oom`** (0 kill; 1 panic; 2 panic even for a cgroup OOM), both from the kernel documentation.

### 5.2 Who dies: badness and `oom_score_adj`

For each candidate process the kernel computes a **badness** score. From memory of `oom_badness()`: points = **resident pages + swap pages + page-table pages**, plus `oom_score_adj` scaled by the total pages in scope (`oom_score_adj × totalpages / 1000`), and the process with the highest score dies, along with a preference for a child of the victim with its own memory. **`oom_score_adj`** ranges from **-1000 to +1000**: **-1000 makes a process immune**, +1000 makes it the first choice, and the value is a **fraction of the total memory in scope** (a value of +100 on a machine with 8 GiB adds about 800 MB of badness). `/proc/PID/oom_score` shows the result scaled to 0 to 1000 (and `oom_score_adj` is also the field the log prints for the victim). The old `oom_adj` is deprecated.

The experiment: a **100 MB cgroup with no swap** (`memory.memsw.limit_in_bytes` equal to the limit) and two Python processes, A holding 40 MB and B growing to 120 MB, both at `oom_score_adj` 0:

```python
# hog.py NAME MB [HOLD_SECONDS]: put MB of anonymous memory in use, in 5 MB steps, then sleep
import sys, time
name, mb = sys.argv[1], int(sys.argv[2]); hold = float(sys.argv[3]) if len(sys.argv) > 3 else 3
keep = []
for i in range(mb // 5):
    c = bytearray(5 << 20)
    for j in range(0, len(c), 4096): c[j] = 1
    keep.append(c); time.sleep(0.01)
print("%s: holding %d MB" % (name, mb), flush=True)
time.sleep(hold)
print("%s: exited normally" % name, flush=True)
```

```text
--- 1. two hogs, same oom_score_adj = 0: A holds 40 MB, B asks for 120 MB in a 100 MB cgroup
bash:  2799 Killed                  python3 $D/hog.py $1 $2 3 > /var/tmp/lx06_$1.out 2>&1
$ cat /proc/$P_A/oom_score 2>&1; cat /proc/$P_B/oom_score 2>&1
670
cat: /proc/2799/oom_score: No such file or directory
  A: exit status 0 ; A: holding 40 MB A: exited normally
  B: exit status 137 ;
$ grep -E "^(oom_kill|under_oom)" $CG/memory.oom_control
oom_kill_disable 0
under_oom 0
oom_kill 1
```

- **B was killed, A survived.** B was the larger and the one that was still asking for memory; A exited normally (status 0). B's status was **137**, which is **128 + 9**: the shell's way of saying "terminated by signal 9 (`SIGKILL`)". **Exit status 137 is the footprint of an OOM kill** (or of any `kill -9`; in a container runtime it is how `OOMKilled` shows up, from memory).
- `memory.oom_control` counted it: **`oom_kill 1`**, and `under_oom 0` afterwards (the condition had cleared).
- The `oom_score` printout is a limit worth noticing. `/proc/PID/oom_score` read **670** for a 40 MB process in a 100 MB group, and in limits item 5 a shell of 3.5 MB read 666: **the number is not reliable as a size ranking in this VM**, so use the order of the kills and the task table below, not the score.

Now the same sizes with **A at `oom_score_adj` 1000** and B (70 MB) at 0:

```text
--- 2. A (40 MB) gets oom_score_adj = 1000, B (70 MB) stays 0: the smaller process dies, B lives
bash:  2808 Killed                  python3 $D/hog.py $1 $2 3 > /var/tmp/lx06_$1.out 2>&1
$ cat /proc/$P_A/oom_score 2>&1; cat /proc/$P_B/oom_score 2>&1
cat: /proc/2808/oom_score: No such file or directory
673
  A: exit status 137 ; A: holding 40 MB
  B: exit status 0 ; B: holding 70 MB B: exited normally
```

**The smaller process was killed**: A (40 MB) got status 137 and B (70 MB), the bigger one, finished normally. By the formula (from memory), the adjustment of +1000 adds the equivalent of the whole cgroup's size (about 25,600 pages for 100 MB) to A's score, which outweighed B's extra 30 MB. This is the tool you use to **protect a database and sacrifice a worker**: raise the adjustment on the expendable process, lower it (to -1000 for immunity, which needs `CAP_SYS_RESOURCE`) on the one that must live. In systemd it is the `OOMScoreAdjust=` unit setting ([LX 04](04_boot_systemd_and_logs.md)).

```text
--- 2b. lowering the value: this sandbox lacks CAP_SYS_RESOURCE
$ echo 500 > /proc/$Q/oom_score_adj && cat /proc/$Q/oom_score_adj
500
$ echo -500 > /proc/$Q/oom_score_adj; echo status=$?
bash: echo: write error: Permission denied
status=1
```

The sandbox accepts a **raise** (500) and refuses a **lower** (-500, `Permission denied`): lowering `oom_score_adj` below the current value needs `CAP_SYS_RESOURCE`, which this sandbox's capability set does not include. That is the kernel's rule on real systems too, from memory: an unprivileged process may raise its own value but not lower it. I could therefore not demonstrate -1000.

### 5.3 Reading the log

```text
--- 3. what the kernel logged
$ dmesg | grep -E "Memory cgroup out of memory|Killed process|oom-kill:" | cut -c1-200 | tail -4
[  766.382372] oom-kill:constraint=CONSTRAINT_MEMCG,nodemask=(null),cpuset=/,mems_allowed=0,oom_memcg=/lx06oom,task_memcg=/lx06oom,task=python3,pid=2799,uid=0
[  766.382382] Memory cgroup out of memory: Killed process 2799 (python3) total-vm:102184kB, anon-rss:69120kB, file-rss:7820kB, shmem-rss:0kB, UID:0 pgtables:228kB oom_score_adj:0
[  769.602900] oom-kill:constraint=CONSTRAINT_MEMCG,nodemask=(null),cpuset=/,mems_allowed=0,oom_memcg=/lx06oom,task_memcg=/lx06oom,task=python3,pid=2808,uid=0
[  769.602924] Memory cgroup out of memory: Killed process 2808 (python3) total-vm:76584kB, anon-rss:44160kB, file-rss:7928kB, shmem-rss:0kB, UID:0 pgtables:176kB oom_score_adj:1000
```

One kill, read field by field. The first line is the **summary**: `oom-kill:constraint=CONSTRAINT_MEMCG` (a cgroup, not the machine), `oom_memcg=/lx06oom` (the cgroup whose limit was hit), `task_memcg=/lx06oom` (the victim's cgroup), `task=python3,pid=...,uid=0`. The second is the **verdict**: `Memory cgroup out of memory: Killed process 2808 (python3) total-vm:76584kB, anon-rss:44160kB, file-rss:7928kB, shmem-rss:0kB, ... oom_score_adj:1000`. Read the three sizes: **`anon-rss` is the heap that was freed by the kill (44,160 kB here)**, `file-rss` is mapped file pages (libraries), `shmem-rss` is shared memory; and `oom_score_adj:1000` is **why this process, not the other**.

Between those lines the kernel prints a **full report**, which is the part to read first on a real incident:

```text
--- 3b. the rest of the report: cgroup counters and the task table for the last kill
$ dmesg | tac | awk "/Memory cgroup out of memory/{f=1} f" | sed -n 2,40p | tac | grep -E "Memory cgroup stats|hierarchical_memory_limit|^.{14}total_(rss|cache) |Tasks state|pid  |\] \[ +[0-9]+\]" | cut -c1-175
[  769.602883] hierarchical_memory_limit 104857600
[  769.602893] Tasks state (memory values in pages):
[  769.602893] [  pid  ]   uid  tgid total_vm      rss rss_anon rss_file rss_shmem pgtables_bytes swapents oom_score_adj name
[  769.602895] [   2808]     0  2808    19146    13022    11040     1982         0   180224        0          1000 python3
[  769.602897] [   2810]     0  2810    24266    17858    15904     1954         0   221184        0             0 python3
```

- **`hierarchical_memory_limit 104857600`** is the limit in bytes (100 MiB): confirms the cgroup was at its configured limit.
- The **task table** lists every process in the scope with `total_vm`, `rss` split into `rss_anon`, `rss_file` and `rss_shmem` (in **pages**, 4 KiB), `pgtables_bytes`, `swapents` and `oom_score_adj`. Here: the process with **`oom_score_adj` 1000 and `rss_anon` 11,040 pages (about 43 MB)** died, and the process with 0 and **15,904 pages (about 62 MB)** lived. The table shows exactly why: the kill follows the **adjusted** score.
- On a **global** OOM the report also prints `Mem-Info:` (the free, active, inactive, slab and swap counters, per zone and per node) and per-zone watermarks; read `free` against `min`, and the `swap` lines to see whether swap was full (from memory of the format).

**Routine for an OOM incident.** (1) `dmesg -T | grep -iE "out of memory|killed process"` or `journalctl -k -g "Out of memory"` ([LX 04](04_boot_systemd_and_logs.md)). (2) Global or cgroup (`constraint`, `oom_memcg`)? (3) Victim's `anon-rss` against the limit: **one process or many?** (4) The task table: is the victim the biggest, or protected by adjustment? (5) Was swap full (global) or `memory.memsw` the limit (cgroup)? (6) What was growing? That is section 9.

### 5.4 Disabling the kill: the process waits instead

`memory.oom_control` in v1 has an `oom_kill_disable` switch. With it set, nothing is killed; **the allocating process stops** and waits for memory to appear:

```text
--- 4. oom_kill_disable = 1: no kill, the allocating process stops and waits
$ cat $CG/memory.oom_control
oom_kill_disable 1
under_oom 1
oom_kill 2
$ grep State /proc/$P_B/status
State:  D (disk sleep)
  B: exit status 0 ; B: holding 120 MB B: exited normally
```

With `oom_kill_disable 1` the cgroup reported **`under_oom 1`** and B's state was **`D (disk sleep)`**: uninterruptible, not killable, not progressing (the `D` state from [LX 03](03_processes_signals_and_proc.md)). Raising the limit let it finish normally. This is a mechanism for a **user-space OOM manager** to decide who dies (it watches the notification and acts), and it is dangerous without one: a process in `D` state that nobody releases is a hang. In cgroup v2 the equivalent control is `memory.oom.group` (kill the whole group together; from memory, below) and there is no kill-disable switch; the user-space tools are **`systemd-oomd`** and **`earlyoom`**, which act on memory pressure before the kernel does (from memory).

### 5.5 What the kernel will not tell you

- **No process is "to blame"** in the log: the victim is the best score, not the one that leaked. A leak in a sidecar can kill the database.
- **A kill frees only `anon-rss` and `shmem-rss`** (and the page tables); the shared file pages it mapped remain in the cache for others.
- **Container runtimes report "OOMKilled"** when the cgroup `oom_kill` count rose (or the exit status was 137); the host's `dmesg` has the detail, and a Kubernetes pod's memory **request** does not limit it but the **limit** does (from memory).

---

## 6. The `vm.*` sysctls, and why `drop_caches` is not a fix

### 6.1 The knobs, with this machine's values

```text
--- the knobs and their values here
$ sysctl vm.swappiness vm.vfs_cache_pressure vm.min_free_kbytes vm.dirty_ratio vm.dirty_background_ratio vm.dirty_expire_centisecs vm.dirty_writeback_centisecs vm.overcommit_memory vm.max_map_count vm.watermark_scale_factor vm.zone_reclaim_mode
vm.swappiness = 60
vm.vfs_cache_pressure = 100
vm.min_free_kbytes = 67584
vm.dirty_ratio = 20
vm.dirty_background_ratio = 10
vm.dirty_expire_centisecs = 3000
vm.dirty_writeback_centisecs = 500
vm.overcommit_memory = 0
vm.max_map_count = 65530
vm.watermark_scale_factor = 10
vm.zone_reclaim_mode = 0
```

| Sysctl | Default here | What it controls (kernel documentation) |
| --- | --- | --- |
| `vm.swappiness` | 60 | relative cost of evicting anonymous against file pages (section 4) |
| `vm.vfs_cache_pressure` | 100 | how eagerly dentry and inode caches are reclaimed; lower keeps them longer, 0 never reclaims them and can cause OOM |
| `vm.min_free_kbytes` | 67,584 | the floor of free memory the kernel keeps; sets the zone `min` watermarks |
| `vm.watermark_scale_factor` | 10 | the gap between the watermarks, in units of 0.01 percent of memory |
| `vm.dirty_background_ratio` | 10 | percent of available memory dirty before the **flusher threads** start writing |
| `vm.dirty_ratio` | 20 | percent dirty at which **the writing process itself** is made to write and wait |
| `vm.dirty_expire_centisecs` | 3,000 | a dirty page older than 30 s is written by the next flusher run |
| `vm.dirty_writeback_centisecs` | 500 | the flusher wakes every 5 s |
| `vm.overcommit_memory` / `overcommit_ratio` | 0 / 50 | section 3 |
| `vm.max_map_count` | 65,530 | the most memory mappings one process may have; debuggers and search engines with many mapped segments raise it (Elasticsearch asks for 262,144; from memory) |
| `vm.zone_reclaim_mode` | 0 | reclaim in the local NUMA zone before using a remote one (section 7) |
| `vm.oom_kill_allocating_task`, `vm.panic_on_oom` | 0, 0 | section 5.1 |

### 6.2 Watermarks: when the kernel starts to reclaim

```text
--- watermarks and what free memory means here
$ grep -E "^(Node 0, zone|        (min|low|high) )" /proc/zoneinfo | head -12 | tr -s " "
Node 0, zone DMA
 min 48
 low 60
 high 72
Node 0, zone DMA32
 min 9790
 low 12237
 high 14684
Node 0, zone Normal
 min 7056
 low 8820
 high 10584
$ grep -E "^(MemFree|MemAvailable|Cached)" /proc/meminfo
MemFree:         7419556 kB
MemAvailable:    7535576 kB
Cached:           334468 kB
```

Each zone has three marks, in **pages**. The `Normal` zone showed `min` 7,056, `low` 8,820, `high` 10,584, and `DMA32` showed 9,790, 12,237 and 14,684; the three `min` values (48, 9,790, 7,056) add up to **16,894 pages, which is 67,576 kB, matching `vm.min_free_kbytes` 67,584**. How they are used (from memory of the kernel's reclaim design):

- **Free memory above `high`:** nothing to do.
- **Free memory falls below `low`:** **`kswapd`**, the background reclaim thread, wakes and reclaims until free memory is back at `high`. The allocating process does not wait. This is the normal steady state of a machine that uses its cache.
- **Free memory falls below `min`:** the allocating process must **reclaim by itself, synchronously** ("direct reclaim"). That is when latency appears, and where a process shows in the `D` state or `sar -B` shows `%vmeff` and `pgscand/s` ([LX 05](05_performance_methodology_and_triage.md) section 4.4). If reclaim fails, the OOM killer runs.

So **a small `MemFree` is not the signal**; **`kswapd` busy** and **direct-reclaim scans** are. Raising `min_free_kbytes` makes `kswapd` start earlier at the price of memory the kernel keeps idle; the value is useful for bursty network allocation (from memory) and dangerous when too large (the documentation warns that a value that is too high can OOM the machine at once).

### 6.3 Dirty pages and writeback

A write returns when the data is in the page cache, and `Dirty` records how much is not yet on disk.

```text
--- dirty pages: 150 MB buffered write into a new file, sampled for about a second, then sync
sample        Dirty  150 MB  Writeback   0 MB
sample        Dirty  150 MB  Writeback   0 MB
sample        Dirty  150 MB  Writeback   0 MB
sample        Dirty  150 MB  Writeback   0 MB
dd returned   Dirty  150 MB  Writeback   0 MB
after sync    Dirty    0 MB  Writeback   0 MB
```

150 MB written to a new file stayed **150 MB dirty** for the whole second sampled, and fell to 0 only at `sync`. Three rules explain it: the background threshold is **10 percent of available memory** (about 750 MB on this machine, my arithmetic from `MemAvailable`), far above 150 MB; the pages were **younger than 30 s**; and the flusher wakes every 5 s. Only when dirty data passes the background ratio does the kernel start writing at once, and only past **`dirty_ratio`** is the writer itself **throttled** (`balance_dirty_pages`), which looks like a write that suddenly blocks. (A first version of this experiment overwrote an existing file and showed `Dirty` falling to 3 MB at once: ext4 flushes a file that was truncated and rewritten when it is closed, a behaviour known as `auto_da_alloc`, from memory. Write to a new name when you measure dirty pages.)

The consequences: **data written without `fsync` can be lost for about 30 s of buffer** if the machine crashes ([OS 16](../05_operating_systems/16_crash_consistency_and_the_page_cache.md)), and a very large `dirty_ratio` on a fast producer makes the **eventual flush a long stall**. Servers with latency targets set `vm.dirty_background_bytes` and `vm.dirty_bytes` (absolute values, which override the ratios) so the flush starts early and stays small.

### 6.4 `drop_caches`: what it does, what it costs

`echo 1 > /proc/sys/vm/drop_caches` frees the **page cache**, `2` the **reclaimable slab** (dentries and inodes), `3` both. It never frees dirty pages (run `sync` first), it cannot free `Shmem` (section 1 measured this), and the documentation says its use outside testing and debugging "is not recommended".

```text
--- warm against cold: read a 300 MB file three ways
cold (after drop_caches)            0.192 s   Cached  476 MB
warm (second read)                  0.054 s   Cached  476 MB
warm (third read)                   0.053 s   Cached  476 MB
cold again (dropped, so paid twice)  0.163 s   Cached  476 MB
```

A 300 MB file read **cold** took **0.19 s**, read again **warm** **0.05 s**, and after another `drop_caches` it took **0.16 s** again: **the cache is a measurable asset** (about 3 times here, on a virtual disk that is very fast; on a real disk it is orders of magnitude). Dropping it **moves the work from memory to I/O and CPU** for every process that used the data: the page faults, the disk reads and, for slab, the rebuilding of dentries and inodes by path walks.

```text
--- slab: walking the tree fills dentries and inodes, drop_caches 2 gives them back
files walked: 308468
SReclaimable: 3 MB -> 56 MB
$ grep -E "^(dentry|ext4_inode_cache|xfs_inode|proc_inode_cache|inode_cache) " /proc/slabinfo | awk "{printf \"%-20s %7d objs %5d bytes each\\n\", \$1, \$2, \$4}"
xfs_inode                  0 objs  1024 bytes each
ext4_inode_cache       33264 objs  1128 bytes each
proc_inode_cache         276 objs   696 bytes each
inode_cache              128 objs   624 bytes each
dentry                 34503 objs   192 bytes each
after echo 2 > drop_caches: SReclaimable 30 MB
```

A `find` over `/usr`, `/lib` and `/opt` (308,468 entries) raised **`SReclaimable` from 3 to 56 MB** (33,264 `ext4_inode_cache` objects of 1,128 bytes and 34,503 `dentry` objects of 192 bytes were live at the end; the cache is bounded by the LRU, not by the walk). `echo 2 > drop_caches` brought it to **30 MB**, not to 3: slab reclaim frees objects that are not referenced and some stay.

**Why "free memory is low, so drop the caches" is wrong.** The kernel already gives the cache back the moment a process needs it (that is what `MemAvailable` counts), so dropping it **frees nothing a process could not already have**, it **loses the work already done**, and it **hides the actual problem** (a leak, a bad limit). There are two correct uses: **benchmarks** (start each run cold, and say so, [LX 05](05_performance_methodology_and_triage.md) section 7.1) and **testing** a cold-start path. A script in `cron` that drops caches every hour is a performance bug with a nice cover story.

---

## 7. Huge pages, THP and NUMA

### 7.1 Why pages of 2 MiB

The page-table walk and the TLB ([OS 12](../05_operating_systems/12_virtual_memory_and_tlb.md)) cost per **page**: mapping 64 MiB with 4 KiB pages needs **16,384 entries and 16,384 first-touch page faults**, and the TLB covers a tiny part of it; with 2 MiB pages it is **32 entries and 32 faults**. Huge pages help programs with large, hot, randomly accessed memory (databases, JVM heaps, in-memory caches, GPUs' host buffers).

Two mechanisms exist:

- **Transparent huge pages (THP):** the kernel decides, per region, to use a 2 MiB page when it can find one. Controlled by `/sys/kernel/mm/transparent_hugepage/enabled` (`always`, `madvise`, `never`) and `defrag`. This VM: **`enabled` is `madvise`, `defrag` is `madvise`**, which means THP applies **only to regions that ask** with `madvise(MADV_HUGEPAGE)`.
- **hugetlb (reserved pages):** a **pool** of pages set aside with `vm.nr_hugepages`, taken with `MAP_HUGETLB` or a `hugetlbfs` mount. They are never swapped, never split, never in the page cache, and unavailable to anything else while reserved.

```c
/* huge.c: 64 MiB anonymous region, touched three ways: plain 4 KiB pages, MADV_HUGEPAGE (THP), MAP_HUGETLB */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/mman.h>

#define LEN (64UL << 20)

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

static long field(const char *key) {                 /* kB from /proc/self/smaps_rollup */
    char line[256]; long v = -1; size_t n = strlen(key);
    FILE *f = fopen("/proc/self/smaps_rollup", "r");
    while (fgets(line, sizeof line, f)) if (!strncmp(line, key, n)) v = atol(line + n + 1);
    fclose(f); return v;
}

static void touch(const char *label, char *p) {
    double t0 = now();
    for (size_t i = 0; i < LEN; i += 4096) p[i] = 1;
    double t1 = now();
    printf("%-30s %6.1f ms   Rss %6ld kB   AnonHugePages %6ld kB   Hugetlb %6ld kB\n",
           label, (t1 - t0) * 1000, field("Rss"), field("AnonHugePages"), field("Private_Hugetlb"));
}

int main(void) {
    char *a = mmap(NULL, LEN, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    touch("4 KiB pages (no madvise)", a);

    char *raw = mmap(NULL, LEN + (2UL << 20), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    char *b = (char *)(((unsigned long)raw + (2UL << 20) - 1) & ~((2UL << 20) - 1));
    int rc = madvise(b, LEN, MADV_HUGEPAGE);
    printf("madvise(MADV_HUGEPAGE) returned %d\n", rc);
    touch("after MADV_HUGEPAGE", b);

    char *c = mmap(NULL, LEN, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    if (c == MAP_FAILED) printf("MAP_HUGETLB failed: %s\n", strerror(errno));
    else touch("MAP_HUGETLB (reserved pool)", c);
    return 0;
}
```

```text
--- 1. no pool reserved: MAP_HUGETLB has nothing to hand out
4 KiB pages (no madvise)         32.5 ms   Rss  67220 kB   AnonHugePages      0 kB   Hugetlb      0 kB
madvise(MADV_HUGEPAGE) returned 0
after MADV_HUGEPAGE              20.6 ms   Rss 132884 kB   AnonHugePages  65536 kB   Hugetlb      0 kB
MAP_HUGETLB failed: Cannot allocate memory
```

```text
--- 2. reserve 40 pages of 2 MiB (80 MB) in the pool, then run again
$ grep -E "^(HugePages_Total|HugePages_Free|Hugetlb)" /proc/meminfo
HugePages_Total:      40
HugePages_Free:       40
Hugetlb:           81920 kB
4 KiB pages (no madvise)         25.4 ms   Rss  67216 kB   AnonHugePages      0 kB   Hugetlb      0 kB
madvise(MADV_HUGEPAGE) returned 0
after MADV_HUGEPAGE               9.6 ms   Rss 132880 kB   AnonHugePages  65536 kB   Hugetlb      0 kB
MAP_HUGETLB (reserved pool)      23.2 ms   Rss 132880 kB   AnonHugePages  65536 kB   Hugetlb  65536 kB
$ grep -E "^(HugePages_Total|HugePages_Free|Hugetlb)" /proc/meminfo
HugePages_Total:      40
HugePages_Free:       40
Hugetlb:           81920 kB
```

- **4 KiB pages:** the 64 MiB region, no `madvise`, showed **`AnonHugePages` 0 kB** and took **32.5 ms** to touch (25.4 ms in the second run).
- **With `MADV_HUGEPAGE`:** the same size showed **`AnonHugePages` 65,536 kB** (every byte of it) and took **20.6 ms** (9.6 ms in the second run): fewer faults, and the kernel zeroes 2 MiB at a time. The saving is real but the numbers swing by a factor of two between runs, so quote it as "2 to 3 times faster to touch".
- **`MAP_HUGETLB` with an empty pool failed** (`Cannot allocate memory`): hugetlb does not fall back, it **needs the pages reserved first**. After `echo 40 > /proc/sys/vm/nr_hugepages` the meminfo showed `HugePages_Total 40`, `HugePages_Free 40`, `Hugetlb 81,920 kB`, and the allocation then succeeded with **`Private_Hugetlb` 65,536 kB** in 23.2 ms. The 80 MB were **taken out of the free memory the moment they were reserved**, whether anyone used them or not, and `HugePages_Free` returned to 40 after the process exited.

**THP in production.** The documentation lists the costs: **latency spikes** when `defrag` is `always` (the fault stalls to compact memory), **memory bloat** (a process that touches one byte of a 2 MiB region gets all 2 MiB), and background **`khugepaged`** collapsing pages. Many databases (MongoDB, Redis, Oracle, some Postgres setups) recommend **`never`** or **`madvise`** for exactly those reasons (from memory); `madvise`, this machine's default, is the safe setting because only code that asked gets THP. Check with `grep AnonHugePages /proc/meminfo`, and per process in `smaps_rollup`.

### 7.2 NUMA

On a multi-socket server each socket has its own memory, and a **remote access costs more than a local one** ([OS 13](../05_operating_systems/13_numa.md)). The tools:

```text
--- 3. NUMA on this VM
$ numactl --hardware
available: 1 nodes (0)
node 0 cpus: 0 1
node 0 size: 4698 MB
node 0 free: 3961 MB
node distances:
node   0
  0:  10
$ numastat -m | head -6 | cut -c1-70
```

This VM has **one node** (`available: 1 nodes (0)`, `node distances: 0: 10`), so there is nothing to place; the tools run and the output is degenerate. What they do on a real machine (from memory):

- `numactl --hardware` lists nodes, their CPUs, sizes, free memory and the **distance matrix** (10 is local; 21 or more is remote).
- `numactl --cpunodebind=0 --membind=0 cmd` pins a command's threads and memory to node 0; `--interleave=all` spreads pages across nodes (used for databases whose memory is shared by threads on all nodes).
- `numastat -p PID` and `numastat -m` give **per-node** memory use; **`numa_miss` and `other_node`** counters show allocations that landed on the wrong node.
- The **default policy is "local first"**: memory is allocated on the node of the CPU that first touched it. A thread started on one node and later scheduled on another reads remote memory forever. **`vm.zone_reclaim_mode`** (0 here) decides whether the kernel reclaims locally before using another node; 0 is right for most workloads.
- `/proc/PID/numa_maps` shows where each mapping's pages are (from memory); automatic NUMA balancing (`kernel.numa_balancing`) migrates pages toward the CPUs that use them.

---

## 8. Cgroup memory limits and memory pressure

Containers get their memory limit from the **cgroup memory controller**, and it is where most real OOM kills happen (section 5). This VM has **cgroup v1**; current distributions and Kubernetes use **v2**. The two have the same ideas under different names.

| Idea | v1 file (measured here) | v2 file (**from memory**, documentation fetched) |
| --- | --- | --- |
| hard limit on memory | `memory.limit_in_bytes` | `memory.max`: if usage reaches it and cannot be reduced, the OOM killer runs inside the group |
| a limit that throttles, never kills | not present | **`memory.high`**: above it the group's processes are throttled and reclaimed hard, but not killed |
| memory plus swap | `memory.memsw.limit_in_bytes` | `memory.swap.max` (swap alone) |
| a floor that reclaim avoids | not present (soft limit is similar) | `memory.low` (best effort) and `memory.min` (hard protection) |
| counter of hits | `memory.failcnt` | **`memory.events`**: `low`, `high`, `max`, `oom`, `oom_kill` |
| kill or wait | `memory.oom_control` (`oom_kill_disable`, `under_oom`, `oom_kill`) | `memory.events` and **`memory.oom.group`** (1 = kill the whole group together) |
| breakdown | `memory.stat`: `cache`, `rss`, `swap` | `memory.stat`: `anon`, `file`, `kernel`, `slab`, `shmem` |
| pressure | not per-group (`memory.pressure_level` events) | **`memory.pressure`**: PSI `some` and `full` lines for the group |
| per-group swappiness | `memory.swappiness` | removed |

**What the experiments measured (v1):** the limit is enforced on **page cache as well as anonymous memory** (section 4.2: reading a 200 MB file in a 120 MB group filled the group to 119 MB with cache and `failcnt` rose to 186, with no kill, because cache can be reclaimed); the memory-plus-swap limit decides when swap stops helping (section 5.2, where equal limits meant "no swap"); and **`failcnt` counts every time the limit was reached**, including the harmless ones: 110,495 hits in section 4.1 with no kill, because the pages could be swapped. **A rising `failcnt` is pressure, not a failure**; `oom_kill` rising is a failure.

**Three habits for containers (the kernel documentation and common practice).**

- **Memory limit set from PSS or from the working set**, not from `RSS` of one process: the cgroup counts cache, kernel memory and tmpfs, all of them against the limit. A container that writes logs to a tmpfs, or reads large files, **grows its usage without growing its heap**.
- **Prefer `memory.high` below `memory.max` in v2** (from memory): the group is throttled and visible as pressure before it is killed, which gives a chance to react.
- **Read the cgroup's own file, not the host's**: `free` and `/proc/meminfo` inside a container normally show the **host**, not the limit (from memory; `lxcfs` is one tool that fakes the files per container). Programs that size a pool from `MemTotal` size it for the host and get killed (Java before container support did exactly this).

### 8.1 Memory pressure, PSI

**Pressure stall information** (`/proc/pressure/memory`, and `memory.pressure` per cgroup in v2) reports the percent of time tasks were **stalled waiting for memory**: `some` is "at least one task stalled", `full` is "all non-idle tasks stalled" ([LX 05](05_performance_methodology_and_triage.md) section 5). It is the number that rises **before** an OOM kill and before swap rates mean anything. [LX 05](05_performance_methodology_and_triage.md) measured it: a 64 MB cgroup reading a 200 MB file moved the memory PSI `total` from **322 to 81,623 µs** with 29,205 limit hits. The practical alert is **memory PSI `some avg60` above a few percent for minutes**, which is a machine or a container that is reclaiming constantly; `systemd-oomd` kills on that signal (from memory).

---

## 9. Finding a leak

**Leak or growth?** A leak is **memory that grows with no matching increase in work and never returns**. The first job is to tell it from a cache that is still warming (which plateaus), a fragmentation pattern (RSS stays high after frees, a property of the allocator) and a workload that really grows. The procedure has two parts, and the first decides which kernel interface to use.

### 9.1 A user-space leak: samples, then a diff

Sample **`smaps_rollup`** at a fixed interval and look at the **slope**:

```text
--- 1. sample Rss and Pss_Anon every second: a steady slope is a leak, a plateau is not
t=1s  Rss:  35444 kB   Pss_Anon:  27856 kB
t=2s  Rss:  39540 kB   Pss_Anon:  31952 kB
t=3s  Rss:  43636 kB   Pss_Anon:  36048 kB
t=4s  Rss:  47732 kB   Pss_Anon:  40144 kB
```

`Rss` and `Pss_Anon` rose by **4,096 kB every second** (35,444, 39,540, 43,636, 47,732 kB): a **steady slope is a leak**; a curve that flattens is a cache. `Pss_Anon` is the better line because it excludes shared libraries. Extrapolate with the slope: at 4 MB/s a 200 MB cgroup lasts about 45 s (Problem E measured it). Now find **which mapping** grows, with two `pmap -x` dumps and a diff:

```python
# mapdiff.py PID_BEFORE_FILE PID_AFTER_FILE: two `pmap -x` dumps, print the mappings whose RSS grew
import sys
def load(p):
    d = {}
    for l in open(p):
        f = l.split()
        if len(f) >= 6 and f[0][0] in "0123456789abcdef" and f[1].isdigit():
            d[f[0]] = (int(f[2]), " ".join(f[5:]))
    return d
a, b = load(sys.argv[1]), load(sys.argv[2])
rows = []
for k, (rss, name) in b.items():
    rows.append((rss - a.get(k, (0, ""))[0], k, rss, name))
rows.sort(reverse=True)
print("%-18s %9s %9s  %s" % ("address", "RSS kB", "grew kB", "mapping"))
for g, k, rss, name in rows[:3]:
    print("%-18s %9d %9d  %s" % (k, rss, g, name))
print("mappings that did not grow: %d" % sum(1 for r in rows if r[0] == 0))
```

```text
--- 2. which mapping is growing: pmap -x twice, 3 seconds apart, diffed
address               RSS kB   grew kB  mapping
00000000388a9000       54296     12288  [ anon ]
ffffffffff600000           0         0  [ anon ]
00007ffec6020000         108         0  [ stack ]
mappings that did not grow: 33
```

One mapping, `[ anon ]` at `0x388a9000` (a low address, so the **`brk` heap**), grew **12,288 kB in 3 s** (the same 4 MB per second); the other 33 mappings did not move. Reading it:

- **A growing `[ anon ]` or `[ heap ]`** is the allocator: the leak is in the process's own allocations. For C and C++, **`valgrind --leak-check=full`** (slow, often 10 to 50 times) or **`LD_PRELOAD` with `heaptrack` or `jemalloc` profiling** (from memory) names the call stack; for Python, `tracemalloc`; for Java, a heap dump. **`perf` and `bpftrace` (`uprobe` on `malloc` and `free`) are the low-overhead way and are in LX 10 and LX 11**.
- **A growing file-backed mapping** is mapped files (a log being `mmap`ed) or `Pss_File` page cache.
- **A growing `[ stack ]`** is deep recursion or thousands of threads (`KernelStack` and `PageTables` in section 1 grow with them).
- **Many new mappings of the same size** (count rising, `vm.max_map_count` getting near) is a leak of mappings, `mmap` without `munmap`; `wc -l /proc/PID/maps` over time shows it.
- **RSS grows but `Pss_Anon` does not** means page cache or shared memory (`Shmem`), not the heap.

### 9.2 A kernel leak: slab

If **the processes' memory is flat and `MemAvailable` still falls**, the kernel is holding it, and `Slab` or `SUnreclaim` in `/proc/meminfo` will show it. `slabtop` and `/proc/slabinfo` name the cache:

```text
--- 3. the same idea in the kernel: slab growth while 100000 files exist
before:
  ext4_inode_cache      8738 objs
  dentry                2081 objs
  SReclaimable:            6 MB
  SUnreclaim:             19 MB
after creating 100000 empty files:
  ext4_inode_cache    100254 objs
  dentry              101619 objs
  SReclaimable:          134 MB
  SUnreclaim:             23 MB
$ slabtop -o -s c | sed -n 8,12p | cut -c1-90
100268 100268 100%    1.10K   7162       14    114592K ext4_inode_cache
101640 101640 100%    0.19K   4840       21     19360K dentry
101760 101760 100%    0.03K    795      128      3180K lsm_inode_cache
   616    579  93%    4.00K     77        8      2464K kmalloc-4k
 15450  15450 100%    0.13K    515       30      2060K kernfs_node_cache
after rm and drop_caches 2:
  ext4_inode_cache       324 objs
  dentry               25053 objs
  SReclaimable:            6 MB
  SUnreclaim:             19 MB
```

Before: **8,738 `ext4_inode_cache` and 2,081 `dentry` objects**, `SReclaimable` 6 MB. After 100,000 empty files were created: **100,254 and 101,619 objects**, **`SReclaimable` 134 MB**, and `slabtop` ranked `ext4_inode_cache` first at **114,592 K** (1.10 K per object), then `dentry` (19,360 K) and `lsm_inode_cache` (3,180 K). That is about **1.3 KB of kernel memory per file** for a file that holds no data. After the files were deleted and `echo 2 > drop_caches`, `ext4_inode_cache` fell to 324 and `SReclaimable` to 6 MB; `dentry` stayed at 25,053 (negative dentries, and I did not chase why they survive this). The reading: **reclaimable slab (dentries, inodes) is a cache and grows on purpose**; a **`SUnreclaim` that climbs** (here it stayed at 19 to 23 MB) is a kernel or driver leak, and a named cache that grows without a matching workload (`kmalloc-*`, `nf_conntrack`, `skbuff_head_cache`, `task_struct`) is where to look. A container host that runs `find` or a backup over a large tree, or a service that creates millions of short-lived files, shows exactly this pattern, and the cure is a `vfs_cache_pressure` above 100 or fewer paths, not a reboot.

**The order of the hunt.** (1) `MemAvailable` falling? (2) `AnonPages` or `Slab` or `Shmem`? (3) By process: PSS sampled (section 2), the biggest slope. (4) By mapping: `pmap` diff. (5) By stack: an allocation profiler. (6) Kernel: `slabtop`. (7) Confirm with the fix and a flat slope.

---

## 10. What it costs: the numbers worth remembering

Measured on one VM; quote them as ranges.

| Quantity | Value |
| --- | --- |
| `malloc(1 GiB)`, nothing touched | VSZ +1,048,580 kB (2,724 to 1,051,304), RSS +320 kB |
| touching 10 MiB of it | RSS 11,984 kB |
| 200 MB of heap touched | `AnonPages` +200, `Committed_AS` +200, `MemAvailable` -204 |
| 200 MB file read | `Cached` +200, `MemFree` -199, **`MemAvailable` 0** |
| 100 MB written to tmpfs | `Shmem` +100, `Cached` +101, `MemAvailable` -99 (not droppable) |
| 100 MB `MAP_SHARED\|MAP_ANONYMOUS` | `Shmem` +100, `Mapped` +100, `Cached` +100 |
| 150 MB written without `fsync` | `Dirty` +150, stays dirty past 1 s, 0 after `sync` |
| `echo 3 > drop_caches` | `Cached` -351; tmpfs and shared memory stayed |
| parent and child of 100 MB right after `fork` | RSS 112 and 106, PSS 58 and 53, USS 5 and 1 |
| the same after the child writes 50 MB | USS 55 and 51, PSS 82 and 78, shared 57 and 55 |
| parent plus 4 workers, each writing 10 MB | sum of RSS 535 MB, sum of PSS 151 MB, used memory +39 MB |
| `CommitLimit` at ratio 50 and 90 | 4,108,332 kB and 7,395,000 kB; +1,048,572 kB with 1 GiB swap |
| ask for 6 GiB, then 30 GiB | mode 0 grant, refuse; mode 1 grant, grant; mode 2 (ratio 50) refuse, refuse |
| 180 MB array in a 120 MB cgroup (swap) | first touch 0.32 s, 89 MB out (63 in another run), second pass 0.60 s with 180 MB in, 110,495 limit hits |
| one swap-in on this virtual disk | about 13 µs per 4 KiB page, thrashing included |
| cgroup OOM kill | exit status 137, `oom_kill` +1, the log names `oom_memcg`, `anon-rss` and `oom_score_adj` |
| `oom_score_adj` +1000 on a 40 MB process, 70 MB peer at 0 | the 40 MB one dies |
| `oom_kill_disable` 1 | no kill, `under_oom` 1, the process sits in `D` state |
| 300 MB file read cold, warm, cold again | 0.19 s, 0.05 s, 0.16 s |
| `find` over 308,468 entries | `SReclaimable` 3 to 56 MB; 30 MB after `drop_caches 2` |
| 100,000 empty files | 100,254 inode and 101,619 dentry objects, `SReclaimable` +128 MB, about 1.3 KB per file |
| 64 MiB touched, 4 KiB pages against `MADV_HUGEPAGE` | 32.5 and 25.4 ms against 20.6 and 9.6 ms; `AnonHugePages` 65,536 kB |
| 40 hugetlb pages reserved | 81,920 kB gone from free memory at once; `MAP_HUGETLB` fails without them |
| a leak of 1 MB every 0.25 s | `Pss_Anon` +4,096 kB per second; a 200 MB group killed it at 44.9 s (46.0 in another run) |

---

## 11. Common mistakes

1. **Reading `MemFree` as "what is left"** (section 1): read `MemAvailable`; clean cache is reclaimable.
2. **Adding up RSS across processes** (section 2.2): shared pages count in each; add PSS (measured: RSS sum 535 MB, PSS sum 151 MB).
3. **Sizing from VSZ** (section 2.1): it counts a promise; a 1 GiB `malloc` raised VSZ by 1 GiB and RSS by 320 kB.
4. **Forgetting that `Cached` includes `Shmem` and tmpfs** (section 1): `drop_caches` will not free them, and a full `/dev/shm` is a memory leak that looks like cache.
5. **Treating overcommit as a bug** (section 3): mode 0 is the right default; mode 2 trades a late kill for early `ENOMEM`, and breaks software that assumes `fork` and `malloc` are cheap.
6. **Believing `swappiness` is an on/off switch** (section 4.2): it biases a choice; cold clean file pages go first at any setting.
7. **Assuming used swap is a problem**: parked cold pages are fine; sustained `si` and `so` is the problem.
8. **Thinking a container OOM means the host is out of memory** (section 5.1): a cgroup limit was hit; read `oom_memcg` and the task table.
9. **Reading the OOM victim as the culprit** (section 5.5): the victim has the best score; a small leaking process can cause a large one to die.
10. **Reading `oom_score` as a size** (limits, item 5): in this VM it was 666 for a 3.5 MB shell; use the kill order and the task table.
11. **Raising `oom_score_adj` of the main service to protect it, and nothing else**: every process in the cgroup competes; protect by lowering the one that matters and raising the others, and remember that a negative value needs `CAP_SYS_RESOURCE`.
12. **Using `drop_caches` to "free memory"** (section 6.4): it frees what the kernel would give anyway, and costs the work (0.19 s cold against 0.05 s warm for 300 MB).
13. **Benchmarking without saying whether the cache was cold** (section 6.4, [LX 05](05_performance_methodology_and_triage.md) section 7).
14. **Measuring dirty pages by overwriting a file** (section 6.3): ext4 flushes on close after a truncate; write a new file.
15. **Turning THP to `always` on a database** (section 7.1): memory bloat and compaction stalls; `madvise` or `never`.
16. **Reserving hugetlb pages "just in case"** (section 7.1): they leave free memory immediately (80 MB for 40 pages here).
17. **Reading a container's `free` or `/proc/meminfo` as its limit** (section 8): it shows the host; read the cgroup files.
18. **Setting a container memory limit equal to the heap size**: cache, kernel memory and tmpfs count against the limit too.
19. **Calling every growth a leak** (section 9): a plateau is a cache; a steady slope in `Pss_Anon` is a leak; a rising `SUnreclaim` is the kernel.
20. **Fixing a leak with a restart schedule and calling it done**: the slope measured in section 9.1 tells you the interval and also tells you how long you have.

---

## 12. Questions out loud

Say each answer in 30 to 60 seconds, with one command or one measured number.

1. **`free` shows 200 MB free. Is the machine out of memory?** Not by that number: `MemFree` is idle pages, and clean page cache is reclaimable. Read `MemAvailable` (measured: reading a 200 MB file lowered `MemFree` by 199 MB and `MemAvailable` by 0) and watch pressure: `si` and `so`, PSI memory, `kswapd` and direct reclaim.
2. **What is the difference between `Cached`, `Buffers`, `AnonPages` and `Shmem`?** Cached is file page cache and includes shared memory and tmpfs; Buffers is block-device cache; AnonPages is process heap and stack; Shmem is tmpfs and shared anonymous memory, counted in Cached too and not droppable (measured: 100 MB of tmpfs gave `Shmem` +100 and `Cached` +101).
3. **VSZ, RSS, PSS, USS: which do you use?** VSZ is address space and almost never; RSS counts shared pages in full; PSS divides shared pages by the sharers and adds up correctly; USS is what exiting would free. Add PSS (measured: sum of RSS 535 MB, sum of PSS 151 MB for a parent and four workers).
4. **Why does `malloc(1 GiB)` succeed on a machine with 512 MB free?** The kernel records the mapping and allocates a page on first touch; mode 0 only refuses a request that could never fit (measured: 6 GiB granted and 30 GiB refused on 8 GiB). The kill, if any, comes later at the page fault.
5. **Explain the three overcommit modes.** 0 heuristic refuse the absurd; 1 always yes; 2 never promise more than swap plus ratio times RAM, with `CommitLimit` and `Committed_AS` showing the budget (measured: 4,108,332 kB at ratio 50, and 6 GiB refused in mode 2).
6. **What does fork cost in memory?** Page tables, not data: pages are shared copy-on-write (measured: after `fork` PSS 58 and 53, USS 5 and 1) and each write copies a page (USS 51 and 55 after the child wrote 50 MB). It is why a Redis snapshot child needs headroom for the writes that happen while it runs.
7. **What is swap for, and what does `swappiness` do?** A place for cold anonymous pages; `swappiness` biases anon against file reclaim. It is not on or off (measured: at 0 and 100 the kernel dropped the cold file and kept the array). Judge swap by the rate of `si` and `so`.
8. **What is thrashing?** A working set bigger than memory: pages are swapped in and out as fast as they are used (measured: a 180 MB array in a 120 MB group swapped 180 MB back in on one read pass and took 0.60 s).
9. **When does the OOM killer run, and who does it pick?** When reclaim cannot free memory in the scope (the machine or a cgroup); it kills the highest badness: resident plus swap plus page tables, shifted by `oom_score_adj` (measured: a 69 MB process died before a 40 MB peer, and a 40 MB process at +1000 died before a 70 MB peer).
10. **How do you read an OOM log?** Find `constraint=` (NONE for global, MEMCG for a cgroup) and `oom_memcg`; the `Killed process` line gives `anon-rss`, `file-rss`, `shmem-rss` and `oom_score_adj`; the task table lists every candidate in pages with its adjustment; then ask who was growing.
11. **What does exit status 137 mean?** 128 plus signal 9, `SIGKILL`: the process was killed; in a container with a memory limit it is the usual footprint of an OOM kill (measured: the cgroup's `oom_kill` went up by 1).
12. **How do you protect a process from the OOM killer?** Lower its `oom_score_adj` (`OOMScoreAdjust=` in a unit; -1000 is immune; needs `CAP_SYS_RESOURCE`), raise it on the expendable ones, and give it its own cgroup limit; better, fix the pressure.
13. **Why was my container killed when the host has gigabytes free?** A cgroup memory limit was reached; the host's free memory does not matter. Cache and tmpfs count against the limit, so usage can rise without a bigger heap.
14. **What are `memory.max` and `memory.high` in cgroup v2?** `max` is the hard limit, with the OOM killer inside the group; `high` throttles and reclaims hard but never kills (from memory, from the kernel documentation); `memory.events` counts the hits and `memory.pressure` shows PSI.
15. **Should I run `echo 3 > drop_caches` when memory is low?** No: it frees only what the kernel would release on demand and loses the cache (measured: 0.19 s cold against 0.05 s warm for one file); use it to make a benchmark cold and say so.
16. **What are dirty pages and when are they written?** Page cache changed and not on disk; the flusher starts above `dirty_background_ratio`, every 5 s for pages older than 30 s, and a writer is throttled above `dirty_ratio` (measured: 150 MB stayed dirty for over a second until `sync`).
17. **When would you use huge pages?** Large, hot, randomly accessed memory where TLB misses and page faults matter (measured: 64 MiB touched 2 to 3 times faster with `MADV_HUGEPAGE`); THP in `madvise` mode so only code that asks gets it, hugetlb when you need guaranteed pages and can reserve them.
18. **How do you find a memory leak?** Sample `Pss_Anon` from `smaps_rollup` for the slope (measured: 4,096 kB per second), diff `pmap -x` to find the mapping, then profile the allocator; if the processes are flat and `MemAvailable` falls, `slabtop` for the kernel.
19. **The processes are small and memory is gone. Where is it?** The kernel: `Slab` or `SUnreclaim`, page tables, `Shmem` or a mounted tmpfs, huge pages reserved, or a cgroup's page cache (measured: 100,000 empty files cost 128 MB of reclaimable slab).
20. **What is memory PSI and why use it?** The percent of time tasks were stalled waiting for memory, `some` and `full`; it rises before an OOM kill and does not depend on whether there is swap (measured in LX 05: 322 to 81,623 µs of stall for a 64 MB group reading 200 MB).

---

## Problems

**Where to solve them.** On any Linux machine with `bash`, `smem` or `/proc/PID/smaps_rollup`, `dmesg`, `gcc` and Python 3, as root for the cgroup and swap tests; the cgroup tests use **v1** memory files (on v2, `memory.max`, `memory.high` and `memory.events` replace `memory.limit_in_bytes`, `memory.failcnt` and `memory.oom_control`). **Never test the OOM killer on a machine that matters outside a cgroup.** Run your solution and compare with the stated result before reading the answer.

**How to approach them.** Name the kind of memory first, then the limit, then the rule. Make the thing happen at small scale, read the counter that shows it, and write the one-sentence diagnosis.

### Problem A (E, entry level): free is small

From `/proc/meminfo` compute the memory a new process could get without swapping, using only `MemFree`, `Buffers`, `Cached`, `SReclaimable` and `Shmem`, and compare it with `MemAvailable`. Then read a 200 MB file and show that `MemFree` falls and `MemAvailable` does not. Say which is the better alarm.

### Problem B (E, entry level): what do five workers cost?

A parent holds a 100 MB array and forks four workers; each writes to 10 MB of it. Print the sum of RSS, the sum of PSS and the growth of `MemTotal - MemFree`, and say which number a capacity plan should use.

### Problem C (M, medium): read one OOM line

From the last `Memory cgroup out of memory` line in `dmesg`, print the victim's `anon-rss`, `file-rss`, `total-vm` and `oom_score_adj`, and say in one sentence whether this process was killed because it was large, because it was adjusted, or both.

### Problem D (M, medium): the commit budget

Check that `CommitLimit` equals `(MemTotal - Hugetlb) × overcommit_ratio / 100 + SwapTotal`, then add 1 GiB of swap and show how `CommitLimit` moves. Say what you would change to let a 6 GiB `mmap` succeed in mode 2 without adding swap.

### Problem E (H, hard): predict the kill

Start a process that leaks 1 MB every 0.25 s in a cgroup with a 200 MB limit and no swap. Measure its `Rss` slope from two samples, **predict the time to the kill**, then measure it, and say what makes the prediction wrong.

---

## Answers

The answers were run, and the checks are in the output.

```bash
#!/usr/bin/env bash
# ans_prob.sh: the measured parts of Problems A to E
. "$(dirname "$0")/h.sh"
D=$(dirname "$(readlink -f "$0")")
echo '=== A: free is small, is the box in trouble?'
python3 - <<'PY'
m = {l.split(":")[0]: int(l.split()[1]) for l in open("/proc/meminfo")}
print("MemTotal %d  MemFree %d  MemAvailable %d  (MB)" % (m["MemTotal"]//1024, m["MemFree"]//1024, m["MemAvailable"]//1024))
print("Cached %d  Buffers %d  SReclaimable %d  Shmem %d  (MB)" % (m["Cached"]//1024, m["Buffers"]//1024, m["SReclaimable"]//1024, m["Shmem"]//1024))
print("free + Buffers + Cached + SReclaimable - Shmem = %d MB against MemAvailable %d MB" % ((m["MemFree"]+m["Buffers"]+m["Cached"]+m["SReclaimable"]-m["Shmem"])//1024, m["MemAvailable"]//1024))
PY
echo; echo '=== B: five processes, one parent and four forked workers: what is the real cost?'
python3 $D/workers.py
echo; echo '=== C: read one OOM kill line from the kernel log'
dmesg | grep 'Memory cgroup out of memory' | tail -1 | cut -c1-200 > /var/tmp/lx06_oomline
cat /var/tmp/lx06_oomline
python3 - <<'PY'
import re
l = open("/var/tmp/lx06_oomline").read()
g = lambda k: int(re.search(k + r":(\d+)kB", l).group(1))
print("victim anon-rss %d kB, file-rss %d kB, total-vm %d kB" % (g("anon-rss"), g("file-rss"), g("total-vm")))
print("oom_score_adj of the victim: %s" % re.search(r"oom_score_adj:(-?\d+)", l).group(1))
PY
rm -f /var/tmp/lx06_oomline
echo; echo '=== D: CommitLimit is a formula, check it, then move it with swap'
python3 - <<'PY'
m = {l.split(":")[0]: int(l.split()[1]) for l in open("/proc/meminfo")}
r = int(open("/proc/sys/vm/overcommit_ratio").read())
print("(MemTotal %d kB - hugetlb %d kB) * %d%% + swap %d kB = %d kB; CommitLimit says %d kB" % (m["MemTotal"], m.get("Hugetlb", 0), r, m["SwapTotal"], (m["MemTotal"]-m.get("Hugetlb",0))*r//100 + m["SwapTotal"], m["CommitLimit"]))
PY
dd if=/dev/zero of=/var/tmp/lx06_sw bs=1M count=1024 status=none; chmod 600 /var/tmp/lx06_sw; mkswap /var/tmp/lx06_sw >/dev/null; swapon /var/tmp/lx06_sw
grep -E '^(SwapTotal|CommitLimit)' /proc/meminfo
swapoff /var/tmp/lx06_sw; rm -f /var/tmp/lx06_sw
echo; echo '=== E: a leak of known rate in a 200 MB cgroup: predict the kill time, then measure it'
CG=/sys/fs/cgroup/memory/lx06leak; mkdir -p $CG; echo 200M > $CG/memory.limit_in_bytes; echo 200M > $CG/memory.memsw.limit_in_bytes 2>/dev/null
python3 $D/leaker.py & L=$!; echo $L > $CG/tasks
t0=$(date +%s.%N); sleep 1
r1=$(awk '/^Rss:/{print $2}' /proc/$L/smaps_rollup); sleep 2; r2=$(awk '/^Rss:/{print $2}' /proc/$L/smaps_rollup)
echo "Rss after 1 s: $((r1/1024)) MB, after 3 s: $((r2/1024)) MB, slope $(( (r2-r1)/2048 )) MB/s"
until ! kill -0 $L 2>/dev/null; do sleep 0.1; done
t1=$(date +%s.%N)
wait $L 2>/dev/null; st=$?; echo "killed after $(echo "$t1 - $t0" | bc | cut -c1-4) s, exit status $st"
rmdir $CG
```

```python
# workers.py: a parent holds 100 MB, forks 4 workers that each write to 10 MB of it; report sums
import os, sys, time
MB = 1 << 20
big = bytearray(100 * MB)
for i in range(0, len(big), 4096): big[i] = 1
def meminfo(k):
    for l in open("/proc/meminfo"):
        if l.startswith(k + ":"): return int(l.split()[1]) / 1024.0
used0 = meminfo("MemTotal") - meminfo("MemFree")
r, w = os.pipe(); kids = []
for n in range(4):
    pid = os.fork()
    if pid == 0:
        for i in range(n * 10 * MB, (n + 1) * 10 * MB, 4096): big[i] = 2
        os.write(w, b"x"); time.sleep(3); os._exit(0)
    kids.append(pid)
for _ in kids: os.read(r, 1)
def roll(pid, key):
    for l in open("/proc/%d/smaps_rollup" % pid):
        if l.startswith(key + ":"): return int(l.split()[1]) / 1024.0
procs = [os.getpid()] + kids
print("sum of RSS over 5 processes: %5.0f MB" % sum(roll(p, "Rss") for p in procs))
print("sum of PSS over 5 processes: %5.0f MB" % sum(roll(p, "Pss") for p in procs))
used1 = meminfo("MemTotal") - meminfo("MemFree")
print("MemUsed (MemTotal - MemFree) grew by: %5.0f MB" % (used1 - used0))
for p in kids: os.kill(p, 15)
for p in kids: os.waitpid(p, 0)
```

```text
=== A: free is small, is the box in trouble?
MemTotal 8024  MemFree 7394  MemAvailable 7398  (MB)
Cached 188  Buffers 33  SReclaimable 4  Shmem 16  (MB)
free + Buffers + Cached + SReclaimable - Shmem = 7605 MB against MemAvailable 7398 MB

=== B: five processes, one parent and four forked workers: what is the real cost?
sum of RSS over 5 processes:   535 MB
sum of PSS over 5 processes:   151 MB
MemUsed (MemTotal - MemFree) grew by:    39 MB

=== C: read one OOM kill line from the kernel log
[  539.779658] Memory cgroup out of memory: Killed process 1739 (python3) total-vm:244504kB, anon-rss:211840kB, file-rss:7892kB, shmem-rss:0kB, UID:0 pgtables:504kB oom_score_adj:0
victim anon-rss 211840 kB, file-rss 7892 kB, total-vm 244504 kB
oom_score_adj of the victim: 0

=== D: CommitLimit is a formula, check it, then move it with swap
(MemTotal 8216668 kB - hugetlb 0 kB) * 50% + swap 0 kB = 4108334 kB; CommitLimit says 4108332 kB
SwapTotal:       1048572 kB
CommitLimit:     5156904 kB

=== E: a leak of known rate in a 200 MB cgroup: predict the kill time, then measure it
Rss after 1 s: 35 MB, after 3 s: 43 MB, slope 4 MB/s
bash:  2216 Killed                  python3 $D/leaker.py
killed after 44.9 s, exit status 137
```

**Problem A.** The sum `free + Buffers + Cached + SReclaimable - Shmem` was **7,605 MB** against `MemAvailable` **7,398 MB**: the estimate is lower by about 207 MB because the kernel keeps its **low-watermark reserve** and counts only part of the cache and slab as safe to take (section 1). The better alarm is **`MemAvailable`** (or, better, **memory PSI** and the reclaim rates), because `MemFree` falls whenever the cache fills (section 1: the file read took 199 MB from `MemFree` and none from `MemAvailable`).

**Problem B.** The sum of RSS was **535 MB**, the sum of PSS **151 MB** and used memory grew by **39 MB** after the fork. The PSS sum is the real figure for the memory held by the five processes: the parent's 100 MB array, the four workers' 40 MB of copied pages and the interpreters (about 11 MB). The growth of used memory is **smaller** because the 100 MB array was already counted before the baseline was taken, and it varied between 39 and 55 MB between runs. A capacity plan uses **PSS for the steady state and a worker's USS for the cost of one more worker**.

**Problem C.** The line printed `anon-rss` **211,840 kB**, `file-rss` 7,892 kB, `total-vm` 244,504 kB and `oom_score_adj` **0**. With the adjustment at 0 the victim was chosen **only by size**: it was the process that leaked (the one from the previous run of Problem E), and the only process in a 200 MB cgroup using most of it. Its `anon-rss` is slightly above the 204,800 kB limit; I did not chase why (batching of the cgroup charge is my guess, from memory). The same question for a **multi-process** cgroup needs the task table printed with the report: look at which rows have the highest `rss_anon` plus `swapents` plus `pgtables_bytes`, and at their `oom_score_adj` (section 5.3).

**Problem D.** The formula gave **4,108,334 kB** against the reported **4,108,332 kB**; with 1 GiB of swap the limit became **5,156,904 kB**, which is the old value plus **1,048,572 kB** (the swap the kernel registered). To allow a 6 GiB mapping in mode 2 **without swap** you raise `vm.overcommit_ratio` to 90 (the limit became 7,395,000 kB in section 3) or set `vm.overcommit_kbytes` to an absolute number; the kernel documentation says setting one disables the other. A 6 GiB request also has to fit **alongside** `Committed_AS` (598,660 kB there).

**Problem E.** The slope was **4 MB/s** (35 MB at 1 s, 43 MB at 3 s) and the leak had a head start: predicted time to 200 MB is 1 + (200 - 35) / 4 = **42 s**; the kill came at **44.9 s** (46.0 s in an earlier run), exit status **137**. The prediction is **about 7 percent early**: the slope between the two samples is slightly under 4 MB/s once the loop's own sleep time is counted, the 1 s sample is not the true start, and the limit also counts page cache and kernel memory of the group, which moves the kill earlier or later by a few seconds. Useful as an alert threshold, wrong as a timer: **use the slope to choose the alert, not to predict the second**.

---

## Cheat sheet

```text
meminfo         MemFree = idle;  MemAvailable = free + reclaimable cache and slab - reserve (the one to read);  Cached includes Shmem and tmpfs (not droppable);  AnonPages = heap and stack
Process sizes   VSZ = promised;  RSS = resident, shared counted fully;  PSS = shared / sharers (add these);  USS = private (what exit frees);  /proc/PID/smaps_rollup, smem, pmap -x
Allocation      malloc = a promise;  first touch = page fault = the cost;  fork = page tables copied, pages shared copy-on-write (PSS 58/53, USS 5/1 after fork)
Overcommit      0 heuristic (refuse the absurd)  1 always  2 never: CommitLimit = (RAM - hugetlb) x ratio/100 + swap (ratio 50: 4,108,332 kB here);  Committed_AS = promised
Swap            for cold anonymous pages;  swappiness 0 to 200 biases anon vs file;  judge by si/so rate, not by used;  thrashing = working set > memory (13 us per page-in here)
Reclaim         free < low: kswapd in background;  free < min: direct reclaim (the process waits);  min_free_kbytes sets min;  watch kswapd, pgscand, PSI memory
OOM             global: "Out of memory" CONSTRAINT_NONE;  cgroup: "Memory cgroup out of memory" CONSTRAINT_MEMCG + oom_memcg;  victim = highest badness (rss + swap + pagetables + oom_score_adj x pages/1000);  exit 137 = SIGKILL
oom_score_adj   -1000 immune ... +1000 first (raise is free, lower needs CAP_SYS_RESOURCE);  systemd: OOMScoreAdjust=;  /proc/PID/oom_score_adj;  read the task table in the log
dmesg -T | grep -iE "out of memory|killed process";   journalctl -k -g "Out of memory";   cgroup: memory.oom_control (v1), memory.events oom_kill (v2)
vm.*            swappiness 60  vfs_cache_pressure 100  min_free_kbytes  dirty_background_ratio 10 / dirty_ratio 20 (or the _bytes forms)  dirty_expire 30 s  max_map_count 65530  overcommit_*
drop_caches     1 page cache, 2 slab, 3 both;  sync first;  cannot free Shmem;  cold costs 0.19 s vs 0.05 s warm (300 MB);  for benchmarks, never as a fix
Huge pages      THP: enabled/defrag = madvise here, MADV_HUGEPAGE gave AnonHugePages 65,536 kB;  hugetlb: vm.nr_hugepages reserves at once (40 pages = 81,920 kB);  MAP_HUGETLB fails with an empty pool
NUMA            numactl --hardware, --cpunodebind --membind, --interleave=all;  numastat -p PID;  local-first allocation;  zone_reclaim_mode 0
cgroup v1       memory.limit_in_bytes  memsw.limit_in_bytes  usage_in_bytes  failcnt  oom_control  swappiness  stat (cache rss swap)
cgroup v2       memory.max (kill)  memory.high (throttle)  memory.low/min (protect)  memory.swap.max  memory.events (low high max oom oom_kill)  memory.oom.group  memory.pressure (PSI)   [from memory]
Leak hunt       smaps_rollup slope (Pss_Anon +4,096 kB/s);  pmap -x diff for the mapping;  allocator profile;  flat processes + falling MemAvailable: slabtop, SUnreclaim
Slab            100,000 empty files = 134 MB SReclaimable (1.3 KB per file);  /proc/slabinfo, slabtop -o -s c;  vfs_cache_pressure above 100 reclaims dentries earlier
```

---

## Sources

Fetched in this session (what each confirmed):

- [Control Group v2](https://docs.kernel.org/admin-guide/cgroup-v2.html) (kernel documentation): `memory.min` and `memory.low` as hard and best-effort protection, `memory.high` as a throttle that never invokes the OOM killer, `memory.max` as the hard limit with the OOM killer inside the group, `memory.events` fields (`low`, `high`, `max`, `oom`, `oom_kill`), `memory.oom.group`, `memory.swap.max`, `memory.pressure`, and `memory.stat` keys. **Only the first 100,000 of 143,268 characters were read**, and the extract did not define PSI `some` and `full` (the PSI definitions come from the page cited in [LX 05](05_performance_methodology_and_triage.md)).
- [Overcommit accounting](https://docs.kernel.org/mm/overcommit-accounting.html) (kernel documentation): the three modes in the words used in section 3, the default ratio of 50, `overcommit_kbytes`, and `Committed_AS` beside `CommitLimit` in `/proc/meminfo`. The page gives **no explicit `CommitLimit` formula**; the formula in section 3 is the one I **verified by measurement** (4,108,334 against 4,108,332 kB).
- [Documentation for `/proc/sys/vm/`](https://docs.kernel.org/admin-guide/sysctl/vm.html) (kernel documentation): `drop_caches` values 1, 2 and 3 and the warning against use outside testing, `swappiness` 0 to 200 with the default of 60, `vfs_cache_pressure`, `min_free_kbytes`, `dirty_ratio`, `dirty_background_ratio`, `overcommit_ratio`, `oom_kill_allocating_task`, `panic_on_oom` and `max_map_count`.
- [Transparent hugepage support](https://docs.kernel.org/admin-guide/mm/transhuge.html) (kernel documentation): `enabled` and `defrag` settings, `MADV_HUGEPAGE`, `AnonHugePages`, `khugepaged`, and the latency and bloat downsides.
- [Memory Resource Controller (cgroup v1)](https://docs.kernel.org/admin-guide/cgroup-v1/memory.html) (kernel documentation): `memory.limit_in_bytes`, `memory.memsw.limit_in_bytes`, `usage_in_bytes`, `failcnt`, `oom_control` with `oom_kill_disable`, `under_oom` and `oom_kill`, `memory.swappiness`, and the `memory.stat` fields; the behaviour when the limit is hit (reclaim, then OOM on the largest task).
- [`proc_pid_smaps(5)`](https://man7.org/linux/man-pages/man5/proc_pid_smaps.5.html) (man page): `Rss`, `Pss`, `Shared_Clean`, `Shared_Dirty`, `Private_Clean`, `Private_Dirty`, `Referenced`, `Anonymous`, `Swap`. **The page did not define `AnonHugePages`, `SwapPss` or `smaps_rollup`**, so the explanations of those in sections 2 and 7 come from the measured output and from memory.

Run in this session: procps-ng 4.0.4 (`free`, `vmstat`), `smem`, `pmap` (procps), `slabtop`, `numactl` and `numastat`, gcc 13, Python 3, `util-linux` (`mkswap`, `swapon`), `dmesg`, on Linux 6.18.44 (2 vCPUs, `MemTotal` 8,216,668 kB) as root, cgroup v1 memory controller, no module support. Not available: `perf`, `bpftrace`, `tcpdump`.

From memory, not fetched or not tested in this session: the exact `oom_badness()` arithmetic and the format of a **global** OOM report (`Mem-Info`, `CONSTRAINT_NONE`), the immunity of `oom_score_adj` -1000 (could not be set here), the `MemAvailable` formula, watermark behaviour (`kswapd` at `low`, direct reclaim at `min`), `systemd-oomd` and `earlyoom`, cgroup v2 behaviour beyond the documentation extract, Redis, MongoDB and Elasticsearch settings, `auto_da_alloc`, `lxcfs`, `valgrind` and `heaptrack`, Kubernetes limits and the `OOMKilled` status, `numa_maps` and automatic NUMA balancing, and swap latencies on real disks.

Cross-references: [LX 03](03_processes_signals_and_proc.md) (`/proc`, process states, PSI first seen), [LX 04](04_boot_systemd_and_logs.md) (cgroups, `dmesg`, `OOMScoreAdjust=`), [LX 05](05_performance_methodology_and_triage.md) (memory saturation, PSI, benchmarks and the page cache), [OS 10](../05_operating_systems/10_memory_management.md), [OS 11](../05_operating_systems/11_virtual_memory.md), [OS 12](../05_operating_systems/12_virtual_memory_and_tlb.md), [OS 13](../05_operating_systems/13_numa.md) and [OS 16](../05_operating_systems/16_crash_consistency_and_the_page_cache.md) for the theory under these tools, [CO 13](../04_computer_organization/13_cache_basics.md) for caches, the [debugging track](../16_debugging/00_plan.md); next in this track: [LX 07](00_plan.md) (disks, filesystems and I/O), [LX 08](00_plan.md) (the networking toolbox), [LX 10](00_plan.md) (`perf` and flame graphs), [LX 11](00_plan.md) (`ftrace`, eBPF, `bpftrace`) and [LX 13](00_plan.md) (tuning for latency and throughput).
