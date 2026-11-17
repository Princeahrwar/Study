# LX 10: `perf` and flame graphs

*Part of the [Linux plan](00_plan.md) · Time: about 6 hours · You need: [LX 05](05_performance_methodology_and_triage.md) (triage by resource: find which resource is the bottleneck before you profile), [LX 03](03_processes_signals_and_proc.md) (`/proc`, signals), [LX 06](06_memory_swap_and_the_oom_killer.md) (page faults) and the cache and pipeline background in the [computer organization track](../04_computer_organization/00_plan.md). This tutorial is the profiling step: given a slow program, find the code responsible, change it, and prove the change with a measurement. `perf` is the tool, a flame graph is the picture, and every number below comes from a program that you can rebuild.*

"The service is slow" is answered in two moves. First the triage of LX 05 says **which resource** is the problem (CPU, memory, disk, network, or waiting). Then a profiler says **which code** uses or waits for that resource. This tutorial covers the profiler: counting with `perf stat` (section 2), sampling with `perf record` and `perf report` (3), getting call stacks right (4), drawing and reading flame graphs (5), profiling the time a program is **not** running (6), tracing system calls and scheduling (7), the kernel, page faults, interpreters and false sharing (8), what works inside a VM (9), and the loop from profile to fix (10). Then come the costs, mistakes, questions and five problems.

**Eight honest limits, stated first.**

1. Everything is measured on **one Linux 6.18 VM** (Firecracker, 2 vCPUs, an Intel Xeon at 2.10 GHz as reported by `lscpu`) as **root**, with `perf` **6.8.12** (the Ubuntu 6.8 package, older than the 6.18 kernel; it worked for every feature used here), `valgrind` (cachegrind), `strace` and `gcc`.
2. **There is no hardware performance counter (PMU) in this VM.** `perf stat -e cycles,instructions,cache-misses,branch-misses` prints **`<not supported>`** for all four (section 2), so **IPC, cache miss rates, branch misses, top-down analysis, `perf mem`, `perf c2c` and LBR stacks are not measured**. They are explained from memory and marked so. Where a counter would have answered a question, section 2 uses **cachegrind**, a simulator, and says that its numbers are simulated.
3. Sampling here uses the **software clock event** (`task-clock`, a timer interrupt), so a sample means "this task was running at that instruction when the timer fired". That is correct for **where CPU time goes**, but it **skids** (the reported instruction is a little after the slow one, section 3) and it cannot attribute time to a cache miss.
4. **No Internet tools were used for the flame graph.** A well known third party script set draws flame graphs; this tutorial instead uses **three short scripts written here** (`collapse.py`, `flame.py`, `offcpu.py`, about 30 lines each) so that the whole pipeline from `perf script` to the picture is visible and checked. The slow run's SVG was rendered once in a headless browser and checked by eye (one wide `__strcmp_evex` plateau over `count_words`, `main` and `_start`, with `find_slow` and the PLT stub beside it); the numbers quoted come from the folded text the SVG is drawn from, and the SVG files themselves are not part of the tutorial.
5. **Timing noise is large.** The VM's run times of the same program varied by up to about 60 percent between runs (section 3 shows `no perf` from 0.34 to 0.56 s), so overhead and speedup are quoted as **ranges from repeated runs** and no single run is trusted.
6. There is no `bpftrace`, no `perf probe` dynamic probes (kernel built without kprobes), and no `/sys/kernel/debug/tracing`; those belong to LX 11. `perf trace` and tracepoint sampling (`sched:`, `syscalls:`) do work.
7. The demo programs are small C programs (a word counter, a request loop, a page toucher, a counter pair) and one Python file. They are chosen so that the profile has a clear answer, which a real service rarely does; the reading rules are what transfer.
8. Single machine, single day. The **numbers do not transfer** to your hardware; the **shapes** do.

By the end you can:

- say what `perf stat`, `perf record`, `perf trace` and `perf sched` each answer, and choose among **counting, sampling and tracing** (measured: the same word counter showed **733 ms of task-clock, 88 page faults, 3 context switches** under `perf stat`, and **`<not supported>`** for cycles, instructions, cache misses and branch misses on this VM);
- turn a profile into a **ranked list with self and inclusive time** (measured: `__strcmp_evex` **78.1 percent** self, its PLT stub **16.6 percent**, `find_slow` **4.1 percent**, while the inclusive time of `count_words` was **100 percent**);
- explain **why a stack is wrong** and fix it with frame pointers, DWARF or LBR (measured: with 1,000 samples, **`find_slow` appeared in only 50 stacks** with frame pointers, 32 without, and **964 with DWARF**, at a data file of **8.2 MB against 113 KB**);
- choose a **sampling frequency** and know its cost (measured: `-F 99`, `999`, `9999` and `49999` gave **59, 442, 4,000 and 133,000 samples**; the program ran in **0.34 to 0.56 s** unprofiled and **0.41 to 0.55 s at 999 Hz** but **1.8 to 3.3 s at 49,999 Hz**);
- build, read and compare **flame graphs** and **differential flame graphs** (measured: 12 distinct stacks, the widest `main;count_words;__strcmp_evex` with **1,120 of 1,434 samples**; after the fix the same program ran **about 40 times faster**: 0.73 s against **0.0176 s**);
- profile the **time off the CPU** (measured: a request loop with **706 ms wall time but 83 ms of CPU**: the on-CPU profile blamed `compute` at **94 percent**, the off-CPU profile showed **605 ms in `usleep` and 18 ms in `fsync`**);
- use **`perf trace`** and **`perf sched`** and know their cost (measured: the 200,000 `read` and `write` calls of a `dd` loop ran in **0.056 s**, under `strace -f -c` in **20.5 s** and under `perf trace` in **18.6 s**: about **330 to 370 times slower**);
- profile the **kernel** and **page faults** (measured: a `dd` with `fsync` spent **99.65 percent of its samples in the kernel**, led by `copy_folio_from_iter_atomic` at **24.0 percent**; touching 200 MiB gave **51,263 page faults**, **99.9 percent** at the line that touched the memory) and put **names on interpreter frames** (measured: with `python3 -X perf` the stack read `py::<module> py::outer py::inner`, without it **no stack** contained `py::inner`);
- say what you **cannot** measure inside a VM and what to do about it (PMU events, `perf c2c`, `perf mem`, LBR: section 9).

**Where the numbers come from.** Five experiment scripts (`x1` to `x5`) and a problem-answer script (`ans_prob`) were run in this session on the VM described above. Transcripts print `$ command` before each command's output. In transcripts `words` is the word counter of section 3 (`words slow N` or `words fast N` counts N generated words), `blk` is the request loop of section 6, and `-r 5` means five runs. Numbers not from these runs are marked "from memory" or come from a source at the end.

**Data and timing.** No data sets: the programs generate their own input. Each experiment runs in seconds to a minute; all timings are single sessions of repeated runs and carry the VM noise stated above.

---

## 1. Counting, sampling, tracing

A profiler answers "where" in one of three ways. Pick by the question, because they have very different costs.

| Method | What it does | Answers | Overhead | `perf` command |
| --- | --- | --- | --- | --- |
| **counting** | reads a counter at the start and the end | how many (instructions, misses, faults, context switches) and the ratios between them | nearly zero | `perf stat` |
| **sampling** | interrupts at a rate or after N events and records where it was | where the time or the events are, statistically | low, set by the rate | `perf record`, `perf report`, `perf top` |
| **tracing** | records every event of one kind | the exact sequence, the latency of each one | per event: high for frequent events | `perf trace`, `perf sched`, `perf record -e tracepoint` |

The order of use is fixed: **count first** (`perf stat`: is it CPU bound or waiting, how many page faults and context switches), **sample second** (`perf record`: which function), **trace last and only the event that matters** (a system call, a scheduler switch). A flame graph is a **picture of a sampling profile**: it adds nothing that `perf report` does not know, but it shows the whole shape of the stacks at once.

What a sample is. The kernel arranges an interrupt, either from a **hardware counter overflowing** (every N cycles, or every N cache misses) or from a **software timer** (here: `task-clock`). At the interrupt it records the **instruction pointer**, the process, and optionally the **call stack**. Choose a rate of F samples per second and a program that ran T seconds gives about F x T samples, and each sample stands for 1/F second of one CPU. A function with a share p of the samples used about a share p of the on-CPU time, and the **error of a share** is the usual binomial one: with 400 samples a 5 percent share has a 95 percent range of about 2.9 to 7.1 percent (Problem D, section 14).

**Two time axes.** A CPU profile covers only the time the process **was on a CPU**. If the process waits for a lock, a disk, a timer or the network, **nothing is sampled during the wait**, and the picture can be perfectly accurate and still miss 90 percent of the latency (section 6 measures exactly this).


---

## 2. `perf stat` and the PMU

The program used throughout sections 2 to 5 counts word frequencies. The slow version looks every word up by **linear search** over the words seen so far; the fast version uses a **hash table**. Both give the same answer. It is a small program with an obvious flaw, which makes it a good test of whether the profiler finds the flaw.

```c
/* words.c MODE N: count word frequencies in N generated words; MODE slow = linear search, fast = hash table */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define UNIQ 4000
#define HSIZE 16384
struct entry { char word[16]; long count; };
static struct entry table[UNIQ];
static int nunique;
static int hash_slot[HSIZE];

static unsigned hash(const char *s) { unsigned h = 5381; while (*s) h = h * 33 + (unsigned char)*s++; return h; }

__attribute__((noinline)) static struct entry *find_slow(const char *w) {
    for (int i = 0; i < nunique; i++)
        if (strcmp(table[i].word, w) == 0) return &table[i];
    return NULL;
}
__attribute__((noinline)) static struct entry *find_fast(const char *w) {
    for (unsigned h = hash(w) & (HSIZE - 1);; h = (h + 1) & (HSIZE - 1)) {
        int s = hash_slot[h];
        if (s == 0) return NULL;
        if (strcmp(table[s - 1].word, w) == 0) return &table[s - 1];
    }
}
static void add(const char *w, int fast) {
    strcpy(table[nunique].word, w);
    table[nunique].count = 0;
    nunique++;
    if (fast) {
        unsigned h = hash(w) & (HSIZE - 1);
        while (hash_slot[h]) h = (h + 1) & (HSIZE - 1);
        hash_slot[h] = nunique;
    }
}
static void next_word(char *buf, unsigned *state) {
    *state = *state * 1103515245u + 12345u;
    snprintf(buf, 16, "w%u", (*state >> 8) % UNIQ);
}
__attribute__((noinline)) static void count_words(long n, int fast) {
    char w[16]; unsigned st = 1;
    for (long i = 0; i < n; i++) {
        next_word(w, &st);
        struct entry *e = fast ? find_fast(w) : find_slow(w);
        if (!e) { add(w, fast); e = fast ? find_fast(w) : find_slow(w); }
        e->count++;
    }
}
int main(int argc, char **argv) {
    int fast = argc > 1 && strcmp(argv[1], "fast") == 0;
    long n = argc > 2 ? atol(argv[2]) : 200000;
    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
    count_words(n, fast);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    fprintf(stderr, "run %.3f s\n", (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9);
    long total = 0; for (int i = 0; i < nunique; i++) total += table[i].count;
    printf("%s: %d unique words, %ld total\n", fast ? "fast" : "slow", nunique, total);
    return 0;
}
```

`__attribute__((noinline))` keeps `count_words`, `find_slow` and `find_fast` as separate stack frames; otherwise the compiler merges them into `main` and the profile loses the names. The program times its own loop to stderr (`run 0.xxx s`).

```text
--- 1. perf stat: software counters work, hardware counters are not exposed to this VM
$ perf stat -e task-clock,page-faults,context-switches,cpu-migrations /tmp/lx10w/words slow 200000 2>&1 | grep -E 'task-clock|faults|switches|migrations|elapsed|user|sys' | sed 's/ \+/ /g; s/#.*//'
 733.52 msec task-clock
 88 page-faults
 3 context-switches
 0 cpu-migrations
 0.734087731 seconds time elapsed
 0.732020000 seconds user
 0.000000000 seconds sys
$ perf stat -e cycles,instructions,cache-misses,branch-misses true 2>&1 | grep -E 'cycles|instructions|misses' | sed 's/ \+/ /g'
 <not supported> cycles
 <not supported> instructions
 <not supported> cache-misses
 <not supported> branch-misses
```

`perf stat` runs the program and prints counters for the whole run. The **software events** work: 733.5 ms of `task-clock` (CPU time used, equal to the elapsed 0.734 s so the program is CPU bound, one thread busy), 88 `page-faults`, 3 `context-switches` and 0 `cpu-migrations`. **Hardware events print `<not supported>`**: this VM does not expose the PMU, which is common in cloud VMs unless the provider enables virtualised counters.

What `perf stat` shows on bare metal (from memory), and the reading rules:

- **`cycles` and `instructions`** give **IPC** (instructions per cycle). On a modern x86 core, **IPC below about 1** with high `cache-misses` means the core is **waiting for memory**; IPC above about 2 to 3 means it is executing efficiently and the cure is **fewer instructions**, not fewer misses. The word counter's linear search is an instruction problem: a tight, predictable loop with a high IPC.
- **`cache-misses` and `cache-references`** are last level cache events on Intel (the definitions differ per vendor and per CPU: read the event list for your CPU). **`branch-misses`** over `branches` is the mispredict rate; below 1 to 2 percent is typical.
- **`stalled-cycles-frontend` and `-backend`** split lost cycles into instruction supply and execution or memory. **Top-down analysis** (`perf stat --topdown`, or `toplev` from `pmu-tools`) organises it as retiring, bad speculation, frontend bound and backend bound.
- The PMU has **few counters** (about 4 programmable plus 3 fixed per core, from memory). Asking `perf stat` for more events than counters makes it **multiplex**: it rotates events in and out and **scales** the result; the percentage in brackets at the end of each line shows how long each event was actually counted. A scaled number of a short burst can be wrong.

```text
--- 2. the same run is repeatable: -r 5 gives a mean and a spread
$ perf stat -r 5 -e task-clock /tmp/lx10w/words slow 200000 2>&1 >/dev/null | grep -E 'task-clock|elapsed' | sed 's/ \+/ /g; s/#.*//'
 861.64 msec task-clock
 0.8622 +- 0.0567 seconds time elapsed ( +- 6.58% )
```

`-r 5` repeats the run and prints the **mean and the spread**: 0.862 s with a relative standard deviation of 6.6 percent here. **Always repeat before comparing two numbers**; on this VM a single run could differ from another by 30 percent or more.

```text
--- 3. what the PMU would have told us: cachegrind simulates instructions, cache and branches
$ valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes --cachegrind-out-file=/dev/null /tmp/lx10w/words slow 20000 2>&1 | grep -E 'I +refs|D +refs|D1 +misses|LL misses|Branches|Mispredicts' | sed 's/ \+/ /g; s/==[0-9]*== //'
I refs: 1,449,161,435
D refs: 313,273,048 (267,155,917 rd + 46,117,131 wr)
D1 misses: 11,475,239 ( 11,378,330 rd + 96,909 wr)
LL misses: 4,414 ( 3,049 rd + 1,365 wr)
Branches: 227,380,494 (183,141,811 cond + 44,238,683 ind)
Mispredicts: 3,431,520 ( 3,431,337 cond + 183 ind)
$ valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes --cachegrind-out-file=/dev/null /tmp/lx10w/words fast 20000 2>&1 | grep -E 'I +refs|D1 +misses|Branches' | sed 's/ \+/ /g; s/==[0-9]*== //'
I refs: 16,836,550
D1 misses: 48,880 ( 47,454 rd + 1,426 wr)
Branches: 2,121,118 (1,912,996 cond + 208,122 ind)
```

**Cachegrind** simulates the CPU's caches and branch predictor on the instructions the program executes (a model of a generic machine, not your CPU, and about 50 times slower). For 20,000 words the slow version executed **1.449 billion instructions**, had **11.5 million simulated L1 data cache misses** and 3.4 million branch mispredicts; the fast version executed **16.8 million instructions** (86 times fewer) and 49,000 data misses. This is **simulated**: it is deterministic and independent of the VM's noise, which makes it useful for **comparing two versions** of code when a real PMU is not available, and it confirms that the fix removed work (instructions), not that it shifted misses.

---

## 3. `perf record` and `perf report`

`perf record` samples; `perf report` summarises the file `perf.data`. The default event is a hardware cycle counter; here it falls back to the software clock.

```text
--- 1. perf record samples the running program; perf report ranks where the samples fell
$ perf record -F 999 -g -o slow.data ./words slow 200000 2>&1 | grep -o 'Captured.*samples)'
Captured and wrote 0.144 MB slow.data (1434 samples)
$ perf report -i slow.data --no-children --stdio -g none 2>/dev/null | grep -v '^#' | grep -v '^$' | head -5 | sed 's/ \+/ /g'
 78.10% words libc.so.6 [.] __strcmp_evex
 16.60% words words [.] strcmp@plt
 4.11% words words [.] find_slow
 0.49% words libc.so.6 [.] __printf_buffer
 0.21% words libc.so.6 [.] __printf_buffer_write
```

```text
--- 2. self time against children (inclusive) time: the callers add up their callees
$ perf report -i slow.data --children --stdio -g none 2>/dev/null | grep -v '^#' | grep -v '^$' | grep -E 'count_words|find_slow|strcmp|main' | head -6 | sed 's/ \+/ /g'
 100.00% 0.07% words words [.] count_words
 100.00% 0.00% words libc.so.6 [.] __libc_start_call_main
 100.00% 0.00% words libc.so.6 [.] __libc_start_main
 100.00% 0.00% words words [.] main
 78.10% 78.10% words libc.so.6 [.] __strcmp_evex
 16.67% 16.60% words words [.] strcmp@plt
```

Reading the report:

- **Self** ("overhead" with `--no-children`) is the share of samples whose instruction pointer was **in that function itself**. `__strcmp_evex` (the AVX-512 version of `strcmp` that glibc selected for this CPU) has 78.1 percent and its **PLT stub** `strcmp@plt` 16.6 percent: a call to a shared library function goes through a small jump stub, and samples that land in it are 16.6 percent of the program. Together **94.7 percent of the time is in `strcmp`**.
- **Children** (inclusive) is self plus everything the function called. `count_words` is **100 percent inclusive** and 0.07 percent self: it does nothing itself but calls the code that is slow. **Sort by self to find the instruction hot spot; sort by inclusive to find which of your own functions to change.** The callee is the symptom and the caller (here `find_slow`, called once per word) is the usual place for the fix.
- `--sort dso`, `--sort comm,dso`, `--sort sym`, `--comm NAME` and `--dsos` slice the same data:

```text
--- 4. sort and filter the same data: by shared object, by symbol, by thread
$ perf report -i f999.data --stdio --sort dso 2>/dev/null | grep -v '^#' | grep -v '^$' | head -3 | sed 's/ \+/ /g'
 69.46% libc.so.6
 30.54% words
$ perf report -i f999.data --stdio --sort comm,dso 2>/dev/null | grep -v '^#' | grep -v '^$' | head -3 | sed 's/ \+/ /g'
 69.46% words libc.so.6
 30.54% words words
```

### 3.1 The frequency

```text
--- 1. sampling frequency: more samples, more detail, more overhead (workload: slow 100000)
-F 99: Samples: 59, data file 7948 bytes
-F 999: Samples: 442, data file 23268 bytes
-F 9999: Samples: 4K, data file 189588 bytes
-F 49999: Samples: 133K, data file 5326420 bytes
```

```text
--- 2. overhead: the program times itself (5 runs, min to max seconds) with and without perf record
no perf:         0.342 0.563
-F 99:  0.322 0.641
-F 999:  0.410 0.548
-F 9999:  0.443 0.544
-F 49999:  1.762 3.291
-F 999 -g:       0.404 0.545
dwarf -F 999:    0.311 0.572
```

`-F N` samples N times per second. A 0.45 s program gives about 59 samples at 99 Hz, 442 at 999 Hz, 4,000 at 9,999 Hz and 133,000 at 49,999 Hz; the file grows from 8 KB to 5.3 MB. The program's own run time (5 runs, minimum to maximum) was **0.34 to 0.56 s unprofiled**, **0.32 to 0.64 s at 99 Hz**, **0.41 to 0.55 s at 999 Hz**, **0.44 to 0.54 s at 9,999 Hz**, and **1.8 to 3.3 s at 49,999 Hz**: up to 999 or 9,999 Hz the cost is **inside the noise of the VM**, and at 49,999 Hz the interrupts and the file writing dominate (several times slower). Call stacks at 999 Hz cost no visible extra here (`-g` 0.40 to 0.55 s) and DWARF unwinding at 999 Hz neither (0.31 to 0.57 s, but section 4 shows its data cost).

Rules of thumb (the first two partly from memory): use **99 Hz** for a long running production process (it is the default of many continuous profilers; low overhead), **999 or 1,999 Hz** to profile a test run, and **avoid exactly 100 or 1,000 Hz**, which can **line up with timer ticks and periodic work** and sample the same phase every time (lockstep sampling); 99 and 997 are odd on purpose. The target is **a few hundred to a few thousand samples** for the code of interest: the error of a share goes with the square root of the count (Problem D).

### 3.2 The event

```text
--- 3. the event used: without a PMU perf falls back to a software clock
$ perf record -F 999 -o e.data ./words slow 100000 2>&1 >/dev/null | grep -i -E 'warning|fall|cycles' | head -2; perf report -i e.data --stdio 2>/dev/null | grep -E "event '"
# Samples: 651  of event 'task-clock:ppp'
$ perf record -F 999 -e cpu-clock -o e2.data ./words slow 100000 2>&1 >/dev/null | grep -c Captured
1
```

With a PMU, `perf record` samples `cycles` (CPU time in clock cycles, so the sample rate follows the **frequency scaling** of the core). Here the event is **`task-clock:ppp`** (a software timer; the three `p` request zero **skid**, which this event cannot give). Other events can be the sampling trigger: `-e cache-misses` samples where the misses are, `-e page-faults -c 1` samples every page fault (section 8), `-e sched:sched_switch` records every context switch (section 6), `-e 'syscalls:sys_enter_*'` every system call. **`-c N` samples every N-th event** instead of a time rate.

### 3.3 `perf annotate`: which instruction

```text
--- 5. perf annotate: samples per instruction inside one function
$ perf annotate -i a.data --stdio -s find_slow 2>/dev/null | grep -E '^ +[0-9]+\.[0-9]+' | sort -nr | head -4 | sed 's/ \+/ /g'
 53.09 : 14a0: add $0x18,%rbx
 32.10 : 14af: call 10f0 <strcmp@plt>
 6.17 : 14a9: mov %r12,%rsi
 2.47 : 14bc: mov %rbx,%rax
```

`perf annotate` shows the samples **per instruction** inside a function (here `find_slow`, with `objdump` for the disassembly). **53.1 percent** of the samples landed on `add $0x18,%rbx` (step to the next 24 byte table entry) and **32.1 percent** on `call strcmp@plt`. This shows **skid**: the instruction after a slow one is blamed, because the interrupt is taken when the slow instruction retires. The real cost sits in the loop body (a call to `strcmp` per entry); the exact line matters less than the loop. With a hardware PMU, `:pp` or `:ppp` (precise) events reduce skid, and cycles sampled in a **hot loop with a load** point at the load (from memory).

Two caveats on any CPU profile: **inlining** merges functions (use `noinline` while profiling, or read `perf report --inline`), and **compiler optimisation** moves code, so build with `-O2 -g` (the optimised code plus debug info) and **never profile a `-O0` build** for performance.


---

## 4. Call stacks: why a profile shows the wrong caller

A flat profile says `strcmp` is hot. A **call stack** says who called it. `perf record -g` (or `--call-graph MODE`) records the stack at each sample. Getting the stack requires **unwinding**: walking from the current frame to its caller, to its caller's caller, and so on. There are three methods.

| Method | How | Cost | Works when | Fails when |
| --- | --- | --- | --- | --- |
| **frame pointer** (`-g`, `--call-graph fp`) | follow the chain of saved `%rbp` registers | cheap: a few loads per frame | **every** function keeps `%rbp` as a frame pointer: `gcc -fno-omit-frame-pointer` | libraries built with `-fomit-frame-pointer` (the distribution default), hand written assembly: chain broken or a frame skipped |
| **DWARF** (`--call-graph dwarf`) | copy up to 8 KB of the user stack at each sample, unwind later with the debug info (`.eh_frame`) | **expensive**: large data, slow `perf script` | binaries have unwind tables (almost all do) | very deep stacks (the 8 KB copy truncates), the data file size |
| **LBR** (`--call-graph lbr`) | read the CPU's last branch record register | cheap, but only about 16 to 32 frames and only branches | Intel with a PMU | VMs without it, AMD (it has an own variant, **BRS** and **LBR v2** on newer parts), deep stacks (from memory) |

A fourth route is the kernel's **ORC unwinder**, which the kernel uses for its own stacks (this VM's kernel is built with `UNWINDER_ORC`): that is why the **kernel frames** below are correct even though user frames depend on the method.

```text
--- 4. stacks need unwinding: frame pointers, none, or DWARF
fp: samples 1031, with find_slow in the stack 50, data file 113212 bytes
nofp: samples 877, with find_slow in the stack 32, data file 68960 bytes
dwarf: samples 974, with find_slow in the stack 964, data file 8229056 bytes
```

The experiment records the same slow run three ways and counts the samples whose stack contains **`find_slow`**, the function that calls `strcmp` once per table entry. In about 1,000 samples:

- **frame pointer, program built with `-fno-omit-frame-pointer`**: `find_slow` appears in only **50** stacks, because `strcmp` is a **leaf function in libc built without a frame pointer**. It does not push `%rbp`, so the walk goes from `strcmp` **straight to the caller's caller** and **skips `find_slow`**: the stack reads `main;count_words;__strcmp_evex`, correct but missing one frame. (In the stacks of section 5 the missing frame is exactly the one you want.)
- **no frame pointer anywhere** (the default `-O2`): **32** stacks; chains are cut at the first function that does not keep a frame pointer and several samples have only one or two frames.
- **DWARF**: `find_slow` appears in **964 of 974** stacks. The cost: a data file of **8.2 MB against 113 KB** (72 times larger) for the same number of samples.

The lessons: **a missing intermediate frame is a leaf function without a frame pointer** (libc `memcpy`, `strcmp` and other assembly routines), not a broken profile; **build your own code with frame pointers on** (or use DWARF); and when the **top** of the stack is right but a **middle** frame is missing, fix by DWARF or LBR, not by guessing. Many large organisations now build their whole OS with frame pointers for this reason (from memory: Fedora and Ubuntu rebuilt their distributions with `-fno-omit-frame-pointer` for profiling, accepting a cost of about 1 to 2 percent).

### 4.1 Symbols

A sample is an address. Turning it into `function (file:line)` needs **symbols**: the ELF symbol table for the function name, **debug info** (`-g`, or a separate `-dbg` package or **debuginfod** server) for the line, and for **kernel** addresses `/proc/kallsyms` (needs `kernel.kptr_restrict` 0 or 1 with root, and `perf_event_paranoid` low enough; this VM reads kernel symbols). Failure modes you will meet:

- **hex addresses instead of names**: the binary is **stripped**; keep an unstripped copy or the debug package;
- **`[unknown]` frames**: code without symbols or with a broken unwind;
- **names of a deleted binary**: the profile is read after the binary was rebuilt; `perf` stores build IDs to detect it (`perf buildid-list`);
- **JIT and interpreters**: the code has no ELF symbols. The **perf map** (`/tmp/perf-PID.map`) lists address ranges and names for JIT code. Node, the JVM (with a helper such as `perf-map-agent`), and **CPython 3.12 and later with `-X perf`** write it.

```text
--- 3. an interpreter: perf sees the C interpreter, not the Python functions, unless python is run with -X perf
python3 : distinct stacks containing py::inner: 0, containing _PyEval_EvalFrameDefault: 10
python3 -X perf: distinct stacks containing py::inner: 8, containing _PyEval_EvalFrameDefault: 16
$ grep 'py::inner' py_perf.folded | head -1 | tr ';' '\n' | grep -o '^py::[a-z_<>]*' | tr '\n' ' '; echo
py::<module> py::outer py::inner
```

Plain Python shows only the C interpreter (`_PyEval_EvalFrameDefault`, `_PyObject_Malloc`): **no stack contained the Python function `inner`**. With `python3 -X perf` each Python function gets its own trampoline and a name in the perf map, so the stacks now read `py::<module> py::outer py::inner` (8 distinct stacks contain `py::inner`). That is how to see **which Python function** is slow, not which C routine of the interpreter.

---

## 5. Flame graphs

A **flame graph** draws the sample stacks as stacked boxes: the **x axis is the sample population (width = share of samples), not time**; the **y axis is stack depth**, root at the bottom; the boxes are **sorted alphabetically** so that the same function in different stacks merges. **A wide box is a function that was on the stack for many samples** (its inclusive time). **A wide box with nothing on top of it is where the time was spent (self time)**: look for plateaus at the top.

The pipeline has two steps, and the middle format is the useful part to know:

```text
perf record -F 999 -g ./prog  ->  perf script  ->  collapse  ->  folded stacks  ->  flame graph (SVG)
```

A **folded stack** is one line per distinct stack: frames separated by `;` from the root to the leaf, then a space and a count. It is text, so it can be filtered with `grep`, summed with `awk` and diffed.

```python
#!/usr/bin/env python3
"""collapse.py: read `perf script` text on stdin, print folded stacks 'root;...;leaf COUNT' (one line per distinct stack)."""
import re, sys
from collections import Counter
stacks, frames = Counter(), []
def flush():
    if frames:
        stacks[';'.join(reversed(frames))] += 1
    frames.clear()
for line in sys.stdin:
    if not line.strip():
        flush(); continue
    if line[0] not in ' \t':          # header line: comm pid cpu time: ...
        flush(); continue
    m = re.match(r'\s*[0-9a-f]+ (.+?) \((.*)\)\s*$', line)
    if not m: continue
    sym = re.sub(r'@{2}GLIBC_[0-9.]+', '', re.sub(r'\+0x[0-9a-f]+$', '', m.group(1)))
    if sym == '[unknown]': sym = m.group(2).rsplit('/', 1)[-1] or sym
    frames.append(sym)
flush()
for s, n in sorted(stacks.items()):
    print(s, n)
```

```text
--- 3. folded stacks: one line per distinct stack, the last field is the sample count
$ perf script -i slow.data 2>/dev/null | python3 collapse.py > slow.folded; wc -l < slow.folded; sort -t' ' -k2 -nr slow.folded | head -3 | cut -c1-200
12
_start;__libc_start_main;__libc_start_call_main;main;count_words;__strcmp_evex 1120
_start;__libc_start_main;__libc_start_call_main;main;count_words;strcmp@plt 238
_start;__libc_start_main;__libc_start_call_main;main;count_words;find_slow 59
$ python3 flame.py slow.folded > slow.svg; grep -c '<rect' slow.svg; ls -l slow.svg | awk '{print $5, "bytes"}'
25
4133 bytes
```

The slow run had **12 distinct stacks** in 1,434 samples. The widest, `_start;__libc_start_main;__libc_start_call_main;main;count_words;__strcmp_evex`, holds **1,120 samples (78 percent)**; the PLT stub is next with 238 (17 percent) and `find_slow` itself 59. The SVG of this has 25 boxes. It is a wide tower of one function; the shape is "everything under `count_words`, one wide plateau at the top".

### 5.1 How to read one

1. **Start at the top edge and read for plateaus**: the widest box that has no children is the self time. Here it is `__strcmp_evex`.
2. **Then walk down** the stack under it to find **your** function that called it, and ask why it is called that often. Here, `count_words` calls `find_slow` once per word, and `find_slow` calls `strcmp` once per table entry: a **loop inside a loop**, which is quadratic: 200,000 words x up to 4,000 entries.
3. **Width is inclusive**: a box 30 percent wide means the function and its callees took 30 percent. Fixing it can gain **at most 30 percent** (Amdahl, Problem B), so rank candidates by width.
4. **Many narrow towers** mean the time is spread out; a single tower means one cause. A **flat top with 50 equal narrow boxes** means a loop over many small functions: look one level down.
5. **Colour carries no meaning** in the standard graph (random warm hues), except in a differential graph.
6. **Check the missing frame** (section 4) before trusting a caller.

### 5.2 Fix, measure, and a differential flame graph

The fix is `find_fast`: a hash table instead of a linear search.

```text
--- 5. the fix and its measurement
$ perf stat -r 5 -e task-clock ./words slow 200000 2>&1 >/dev/null | grep elapsed | sed 's/ \+/ /g; s/#.*//'
 0.9345 +- 0.0949 seconds time elapsed ( +- 10.15% )
$ perf stat -r 5 -e task-clock ./words fast 200000 2>&1 >/dev/null | grep elapsed | sed 's/ \+/ /g; s/#.*//'
 0.017631 +- 0.000462 seconds time elapsed ( +- 2.62% )
$ awk '{s+=$NF} END{print "fast (10x the words): samples", s}' fast.folded
fast (10x the words): samples 122
$ sort -t' ' -k2 -nr fast.folded | head -3 | sed 's/;/ ; /g' | rev | cut -c1-120 | rev
_start ; __libc_start_main ; __libc_start_call_main ; main ; count_words ; __strcmp_evex 36
n ; __libc_start_call_main ; main ; count_words ; __snprintf_chk ; __vsnprintf_internal ; __printf_buffer 29
_start ; __libc_start_main ; __libc_start_call_main ; main ; count_words ; find_fast 20
```

The slow version ran in **0.73 to 0.93 s** (0.734, 0.862 and 0.934 s in three separate measurements) and the fast version in **0.0176 s** (mean of 5, relative deviation 2.6 percent): about **40 to 50 times faster**, with the same output. The new profile (of 10 times as many words, so that it has enough samples: 122) shows the time spread over `strcmp`, `snprintf` (the generator of the words, now visible) and `find_fast`: **a flat, spread profile means the big win is done** and the next gain is much smaller work for each step.

A **differential flame graph** shows what changed. It takes the **old** and **new** folded files, matches stacks, and draws the new profile with boxes **coloured by the change**: red where the new run has a larger share of samples, blue where it has a smaller one.

```text
--- 6. a differential flame graph: red where the new profile has more samples, blue where fewer
$ python3 flame.py slow.folded fast1.folded > diff.svg; grep -c '<rect' diff.svg
25
$ head -2 diff.folded | sed 's/;/ ; /g' | rev | cut -c1-90 | rev
tart_main ; __libc_start_call_main ; main ; count_words ; __strcmp_evex 1120 6
bc_start_main ; __libc_start_call_main ; main ; count_words ; strcmp@plt 238 0
```

The comparison file `diff.folded` lists each stack with its old and new count (`... __strcmp_evex 1120 6`: 1,120 samples before, 6 after for the same stack, and `strcmp@plt 238 0`). Normalise by total samples when the two runs differ in length (the drawing script does: it compares **shares**, not counts). A graph that is blue everywhere you changed code and red nowhere is a clean fix; **red** in an unrelated place is a regression or a moved cost.

The SVG renderer used here is `flame.py` (about 35 lines: build a tree from the folded stacks, give each node a width, draw rectangles with `<title>` tooltips). The standard flame graph tools and `perf report --stdio` give the same information; the point of seeing the pieces is that **a flame graph is a view of folded text**, and you can build the view you need: filter a stack, merge by library, collapse recursion, or restrict to samples under one function with `grep`.


---

## 6. Off-CPU analysis: where the time goes when the program is not running

A CPU profile measures time on a CPU. A request that spends most of its life **waiting** (a lock, a sleep, a disk write, a socket, a queue behind another process) is invisible to it. The test program is a request loop that does three things per request: a computation, a 30 ms wait for a "backend" and a log write with `fsync`.

```c
/* blocker.c: a request loop that is slow for two reasons: it computes (on CPU) and it waits (off CPU) */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

__attribute__((noinline)) static unsigned compute(unsigned x) {
    for (int i = 0; i < 3000000; i++) x = x * 1664525u + 1013904223u;
    return x;
}
__attribute__((noinline)) static void wait_for_backend(void) { usleep(30000); }
__attribute__((noinline)) static void write_log(int fd) {
    char buf[4096] = {1};
    write(fd, buf, sizeof buf);
    fsync(fd);
}
__attribute__((noinline)) static void handle_request(unsigned *acc, int fd) {
    *acc = compute(*acc);
    wait_for_backend();
    write_log(fd);
}
int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 20;
    int fd = open("/var/tmp/lx10_blocker.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    struct timespec t0, t1; unsigned acc = 1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < n; i++) handle_request(&acc, fd);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    fprintf(stderr, "wall %.3f s\n", (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9);
    close(fd); unlink("/var/tmp/lx10_blocker.log");
    printf("done %u\n", acc != 0);
    return 0;
}
```

```text
--- 1. a request loop: wall time against CPU time
$ perf stat -e task-clock ./blk 20 2>&1 | grep -E 'wall|task-clock|elapsed' | sed 's/ \+/ /g; s/#.*//'
wall 0.706 s
 82.94 msec task-clock
 0.708312693 seconds time elapsed
```

The loop ran 20 requests in **0.706 s of wall time** and used **82.9 ms of CPU** (12 percent): the process was running about one time in eight. Now profile it the usual way:

```text
--- 2. the on-CPU profile only sees about 12 percent of the time the process was running
$ perf report -i on.data --no-children --stdio -g none 2>/dev/null | grep -v '^#' | grep -v '^$' | head -3 | sed 's/ \+/ /g'
 93.90% blk blk [.] compute
 1.22% blk [kernel.kallsyms] [k] __list_del_entry_valid_or_report
 1.22% blk [kernel.kallsyms] [k] _find_first_bit
```

The CPU profile is correct and useless for the latency: **`compute` is 94 percent of the samples**, and making it free would speed the loop up only **1.13 times** (Problem C), because 88 percent of the wall time is something else. This is the standard trap of CPU profiling: a profile of a **mostly idle** process describes the idle process's small busy part.

### 6.1 The off-CPU profile

To measure waiting, record **every scheduler switch**: when a task leaves the CPU, note the time and the stack (the stack **at the moment of blocking**, which shows what it waits for), and when it comes back, add the elapsed time to that stack. The tracepoint is `sched:sched_switch`, recorded **system wide** (`-a`) because the "comes back" event happens in the context of the CPU's idle task, not of the waiting task. The script `offcpu.py` does the pairing and writes **folded stacks whose count is microseconds off CPU**, so the same flame graph code draws it.

```python
#!/usr/bin/env python3
"""offcpu.py COMM: read `perf script -F comm,tid,time,event,trace,ip,sym` of sched:sched_switch on stdin;
print folded stacks 'state;root;...;leaf MICROSECONDS' for the time tasks named COMM spent off the CPU."""
import re, sys
from collections import Counter
comm = sys.argv[1]
out, acc, lost = {}, Counter(), 0
head = re.compile(r'^(\S+)\s+(\d+)\s+([\d.]+): sched:sched_switch: prev_comm=(\S+) prev_pid=(\d+) prev_prio=\d+ prev_state=(\S+) ==> next_comm=\S+ next_pid=(\d+)')
ev, frames, events = None, [], []
def flush():
    global ev, frames
    if ev:
        events.append((ev[2], ev, list(reversed(frames))))   # per CPU buffers can arrive out of order
    ev, frames = None, []
for line in sys.stdin:
    m = head.match(line)
    if m:
        flush(); ev = (m.group(1), m.group(2), float(m.group(3)), m.group(4), m.group(5), m.group(6), m.group(7)); continue
    m = re.match(r'\s+[0-9a-f]+ (\S+)', line)
    if m and ev: frames.append(re.sub(r'(\+0x[0-9a-f]+)?(@.*)?$', '', m.group(1)))
flush()
for t, ev, stack in sorted(events, key=lambda e: e[0]):
    c, tid, t, pcomm, ppid, state, npid = ev
    if npid in out:                                           # a task we saw leaving is coming back
        t0, stk, st = out.pop(npid)
        acc[st + ';' + ';'.join(stk)] += int((t - t0) * 1e6)
    if pcomm == comm:
        if ppid in out: lost += 1                              # left twice without coming back: an event was lost
        out[ppid] = (t, stack, state)
for s, n in acc.most_common(): print(s, n)
print('unmatched switch-outs: %d' % (len(out) + lost), file=sys.stderr)
```

```text
--- 3. the off-CPU profile: time between sched_switch out and back in, by stack
recordings needed: 4 (unmatched switch-outs in the last one: 1 of about 85)
$ awk '{n=split($1,a,";"); st=a[1]; t[st]+=$NF} END{for(s in t) printf "state %s: %.0f ms\n", s, t[s]/1000}' off.folded | sort
state D: 18 ms
state S: 605 ms
$ awk '{print $NF, $1}' off.folded | sort -nr | head -1 | sed -E 's/__libc_start_main;//; s/entry_SYSCALL_64_after_hwframe;do_syscall_64;//' | cut -c1-150
605435 S;_start;__libc_start_call_main;main;handle_request;usleep;__nanosleep;clock_nanosleep;__x64_sys_clock_nanosleep;common_nsleep;hrtimer_nanoslee
$ awk '/usleep/{a+=$NF} /;fsync;/{b+=$NF} END{printf "off CPU under usleep %.0f ms, under fsync %.0f ms\n", a/1000, b/1000}' off.folded
off CPU under usleep 605 ms, under fsync 18 ms
```

A warning about the recording itself: on this VM some `perf record -a -e sched:sched_switch` runs **lost the switch-in events** of one CPU (the file held 85 switch-outs of `blk` but as few as 12 switch-ins), which makes the off-CPU total far too small (one attempt gave 30 ms instead of 600). The script therefore **repeats the recording until at most 12 of the 85 switch-outs are unpaired**; the frozen run needed 4 attempts (first line of the transcript). Always **check that the off-CPU total is close to the wall time minus the CPU time** before believing it; here 605 + 18 ms against 706 - 83 = 623 ms.

The total off-CPU time of the task: **605 ms in state `S`** (interruptible sleep: a timer) and **18 ms in state `D`** (uninterruptible: waiting for I/O). The widest stack ends in `clock_nanosleep` called from `usleep`, called from `handle_request`: **605 ms under `usleep` and 18 ms under `fsync`** (whose kernel stack goes through `ext4_sync_file` and the block layer, `io_schedule`). Compare with the wall time of 706 ms: the off-CPU profile accounts for 88 percent of it and the on-CPU profile for 12 percent, together all of it. **Add the two profiles to explain the wall time.** The decision follows: shortening the 30 ms wait (batching, a faster backend, running requests concurrently) would give **7.0 times**; optimising `compute` gives 1.13 times.

An **off-CPU flame graph** is a flame graph of that file: width is **time blocked**, not time running. Read it the same way. Two warnings: it contains **everything that blocks**, including waits that are not a problem (a worker thread idle in `futex_wait` waiting for work, a server blocked in `accept`), so **focus on the stacks of the thread that serves the request**; and the volume of events is high on a busy system (every context switch), so the tracing overhead is real (section 7). With a PMU-free kernel the standard way is the tracepoint as here; with eBPF (LX 11) the aggregation can be done **in the kernel** to cut the cost (from memory: `offcputime` from `bcc`, `bpftrace`).

---

## 7. `perf trace` and `perf sched`: tracing system calls and scheduling

**`perf trace`** is a `strace` built on tracepoints: it shows every system call of a process with its arguments and latency, and with `-s` a summary.

```text
--- 4. perf trace -s: system calls by count and time
$ perf trace -s -o t.txt ./blk 20 >/dev/null 2>&1; grep -E 'clock_nanosleep|fsync|write ' t.txt | sed 's/ \+/ /g' | head -4
 clock_nanosleep 20 0 604.366 30.177 30.218 30.274 0.02%
 fsync 20 0 17.280 0.577 0.864 1.715 6.20%
 write 22 0 1.120 0.024 0.051 0.100 5.85%
```

Columns are `calls, errors, total ms, min, avg, max, stddev`. The 20 `clock_nanosleep` calls took **604.4 ms** in total (min 30.18, average 30.22, max 30.27 ms for a requested 30 ms: **a sleep of N always lasts a little more than N**, the timer slack and scheduling). The 20 `fsync` calls took **17.3 ms** in total with an average of **0.86 ms** and a maximum of **1.7 ms**: the maximum, twice the average, is the call that sets the tail latency. The 22 `write` calls took 1.1 ms. This gives per-system-call latency **without** a PMU.

**`perf sched`** records the scheduler's events (`sched_switch`, `sched_wakeup`, `sched_migrate_task`) and reports them. `perf sched latency` summarises delay per task; `perf sched timehist` lists each time a task ran:

```text
--- 5. perf sched: the scheduler view of the same task
$ perf sched timehist -i s.data 2>/dev/null | grep -E 'blk\[' | awk '$4 > 29 && $4 < 32' | sed 's/ \+/ /g' | head -3
 1256.737670 [0000] blk[3103] 30.153 0.040 0.153
```

Each line is: time, CPU, task, **wait time** (how long it was blocked since it last ran), **scheduling delay** (the time between being woken and getting a CPU) and **run time**, in ms. The `blk` task, in the line shown, waited **30.15 ms** (the sleep), was scheduled **0.04 ms** after it became runnable (the scheduler was not the problem: the machine was idle) and ran **0.15 ms** before sleeping again. (Only lines with a wait of about 30 ms are filtered in; other lines of the same record had zero scheduling delay and some of the `sched_switch` events of the record are missing, the same loss described in section 6.1.) **A large scheduling delay with a small wait** means the task was runnable but the CPUs were busy: that is CPU saturation, which the run queue length of LX 05 also shows.

### 7.1 What tracing costs

```text
--- 6. cost of tracing every system call: a loop of 200000 one byte reads
plain:        0.0557004 s,
strace -c:    20.4971 s,
perf trace:   18.5528 s,
```

A loop of 200,000 one byte `read` and 200,000 `write` calls (`dd bs=1`) took **0.056 s**. Under `strace -f -c` it took **20.5 s** and under `perf trace -s` **18.6 s**: **about 330 to 370 times slower** (the time of a system call goes from about 140 ns to more than 40 microseconds). `strace` stops the process twice per call with `ptrace`; `perf trace` uses tracepoints and avoids the stops, but on this VM (no PMU, software timers, copying of each event) it is not much cheaper for a call this short. The lessons: **never run `strace` on a production process that makes many system calls** (it can stop a service), **trace one system call** (`perf trace -e fsync`) or **sample instead of tracing** (`perf record -e syscalls:sys_enter_fsync`), and treat any timing under a tracer as **inflated for short calls**. The latencies of the long calls above (30 ms, under 2 ms) are not affected much, because the overhead per call is a few tens of microseconds.

---

## 8. The kernel, other events, and memory sharing

### 8.1 Kernel time

```text
--- 1. kernel time: perf record -a sees kernel symbols and stacks (workload: dd writing 200 MiB with fsync)
$ perf report -i k.data --no-children --stdio -g none --comm dd --sort dso 2>/dev/null | grep -v '^#' | grep -v '^$' | head -3 | sed 's/ \+/ /g'
 99.65% [kernel.kallsyms]
 0.35% ld-linux-x86-64.so.2
$ perf report -i k.data --no-children --stdio -g none --comm dd 2>/dev/null | grep -v '^#' | grep -v '^$' | head -4 | sed 's/ \+/ /g'
 24.03% [kernel.kallsyms] [k] copy_folio_from_iter_atomic
 2.47% [kernel.kallsyms] [k] read_zero
 1.06% [kernel.kallsyms] [k] mpage_process_page_bufs
 0.71% [kernel.kallsyms] [k] _raw_read_unlock
```

`perf record -a` records the whole system, kernel included (the kernel addresses need `/proc/kallsyms` readable). `--comm dd` keeps one command. A `dd` that writes 200 MiB with `fsync` spent **99.65 percent of its samples in the kernel** (`[kernel.kallsyms]`) and only 0.35 percent in the dynamic loader; the top symbol is **`copy_folio_from_iter_atomic` at 24.0 percent**, the copy of the user's buffer into the page cache. The shape (`[k]` frames dominating) answers "is this process CPU bound in its own code or in the kernel", which `perf stat` shows as `user` against `sys` time. **A high system time** points to system calls, page faults, network or file system work: sample with `-g` and look at the kernel stack to see which system call.

### 8.2 Sampling other events

```text
--- 2. sampling another event: where do page faults happen (touch 200 MiB, one byte per page)
$ perf stat -e page-faults ./touch 200 2>&1 | grep -E 'touched|page-faults' | sed 's/ \+/ /g; s/#.*//'
touched 200 MiB
 51263 page-faults
$ perf report -i pf.data --no-children --stdio -g none --sort sym 2>/dev/null | grep -v '^#' | grep -v '^$' | head -2 | sed 's/ \+/ /g'
 99.88% [.] fill
 0.01% [.] _dl_relocate_object
```

`-e page-faults -c 1` takes one sample at **every page fault**. The program touches 200 MiB, one byte per 4 KiB page: 200 x 256 = 51,200 pages, and `perf stat` counted **51,263 page faults** (the 63 extra are the program's own start up). The page fault profile put **99.9 percent** at `fill`, the function that touches memory. The method generalises: any event can be the clock of a profile. On a PMU: `-e cache-misses` (where do the misses come from), `-e branch-misses`, `-e LLC-load-misses`; for software: `-e context-switches`, `-e major-faults`, `-e syscalls:sys_enter_openat`. This gives **where the events are**, which counting does not.

### 8.3 Cache lines shared between threads

Two threads incrementing **different** counters that sit in the **same 64 byte cache line** make the line bounce between the cores (**false sharing**; the coherence protocol of the computer organization track). The detector on real hardware is **`perf c2c`** (cache to cache): it samples loads and stores with their data address (a PMU feature: Intel PEBS or AMD IBS), groups them by cache line and shows the lines with the most **HITM** events (a load that hit a modified line in another core's cache) and the offsets and threads involved. `perf mem` records the data addresses. **Neither runs on this VM** (`failed: memory events not supported`), so the commands are from memory:

```text
perf c2c record -F 60000 -a -- ./prog ; perf c2c report --stdio   (from memory, needs a PMU with data address sampling)
```

What can be measured without them is the **effect**, with a timing test:

```text
--- 4. false sharing: two threads, counters in one cache line against 128 bytes apart (perf c2c needs a PMU, so time it)
shared: 0.173 0.216  seconds (min max of 5)
padded: 0.168 0.180  seconds (min max of 5)
```

Two threads, 500 million increments each: with the counters in one line the run took **0.173 to 0.216 s**, with them 128 bytes apart **0.168 to 0.180 s**. **The effect is small and noisy here**, nothing like the several times slowdown that real multi socket hardware can show, perhaps because both vCPUs of this VM may share caches or the hypervisor schedules them on nearby cores. The lesson for the interview is the **method**: suspect false sharing when **threads scale worse than they should** and the hot code is a **store to a shared structure**; confirm with `perf c2c`; fix with **padding or `alignas(64)`** or by **moving per thread counters apart**; and do not trust a single timing from a VM.

---

## 9. What works inside a VM

| Feature | This VM | Needs | Note |
| --- | --- | --- | --- |
| `perf stat` software events (`task-clock`, `page-faults`, `context-switches`) | **works** | nothing | measured |
| `perf stat` hardware events (`cycles`, `instructions`, misses) | **not supported** | a virtualised PMU | EC2 bare metal and many Nitro types, some GCP and Azure sizes offer it |
| `perf record` timer sampling, call graphs fp and DWARF | **works** | nothing | measured |
| precise events (`:pp`, `:ppp`), LBR stacks, PEBS | not run | PMU | skid remains with the software clock |
| `perf trace`, `perf sched`, tracepoints (`sched:`, `syscalls:`, `block:`) | **works** | tracefs-less perf events | measured |
| `perf annotate`, `perf report --sort` | **works** | `objdump` for annotate | measured |
| `perf mem`, `perf c2c` | **not supported** | PMU with data address sampling | `failed: memory events not supported` |
| kprobes, uprobes, `perf probe` | **kernel has no kprobes** | `CONFIG_KPROBES` | `perf list` showed `uprobe` as an event source only |
| eBPF tools (`bpftrace`, `bcc`) | not installed | `CONFIG_BPF_SYSCALL=y` is set | LX 11 |
| cachegrind simulation | **works** | `valgrind` | simulated instructions, caches, branches |

The rule in a VM: **check `perf list` and `perf stat -e cycles true` first**. If the PMU is missing, a software clock profile still finds the hot function and the call stack, which answers most CPU questions; it cannot say **why** (stall or work). For that, use **cachegrind** (simulated), run on **bare metal** or a **VM type that exposes the PMU**, or read the application's own counters.

---

## 10. From a profile to a fix

The loop that wins interviews and incidents:

1. **State the question and the target** ("p99 of this request, 200 ms down to 100 ms"). Without a target you will optimise forever.
2. **Triage the resource** (LX 05): CPU bound (`task-clock` close to the wall time), or waiting (low `CPUs utilized`: off-CPU profile), or system time (kernel profile).
3. **Reproduce** with a **fixed input** and record the baseline with **`perf stat -r N`**.
4. **Profile** with `-g` and stacks that work (section 4). Read **self** for the instruction hot spot and **inclusive** for the function to change.
5. **Find the reason**, not just the function: a loop inside a loop (the word counter), a lock, a copy, a system call per item, a cache miss pattern.
6. **Change one thing**; **re-measure** with the same command; check the **output is the same**.
7. **Re-profile**: the profile after the fix is the next target. A **flat** profile means the big win is done.
8. **Keep the evidence**: the before and after numbers, the differential graph, the commit that explains why.

Pitfalls that give a wrong answer: profiling an **unrepresentative load** (idle service, tiny input), a **debug build**, **one run**, a profile **without waiting** time (section 6), **averages** that hide the tail (profile the slow requests), and **optimising what is wide in the graph but not on the critical path** (a background thread).


---

## 11. What it costs

| Item | Cost (measured here unless marked) |
| --- | --- |
| `perf stat` software counters | none visible: 88 page faults and 3 context switches counted for a 0.73 s run |
| `perf record` at 99 to 9,999 Hz, frame pointer or DWARF stacks | inside the VM noise: the program ran 0.31 to 0.64 s against 0.34 to 0.56 s unprofiled |
| `perf record` at 49,999 Hz | 1.8 to 3.3 s against 0.34 to 0.56 s: several times slower, and 5.3 MB of data for 0.45 s |
| data file | 8 KB (59 samples) to 5.3 MB (133,000); DWARF stacks 8.2 MB for 974 samples (72 times the frame pointer file) |
| off-CPU tracing of every `sched_switch`, system wide | a few hundred events for this idle loop; on a busy server millions per second (from memory), so record for seconds, not hours |
| `strace -f -c` and `perf trace -s` on a call heavy loop | 330 to 370 times slower (0.056 s to 20.5 and 18.6 s) |
| `valgrind --tool=cachegrind` | about 50 times slower (from memory); deterministic counts |
| `perf annotate`, `perf report` | offline, post processing only |
| continuous profiling in production (from memory) | about 99 Hz on every process, usually below 1 percent CPU; needs frame pointers or an eBPF unwinder |

---

## 12. Common mistakes

1. **Profiling before triage.** A CPU profile of a process that mostly waits describes 12 percent of the time (section 6). Look at `CPUs utilized` and the wall against CPU time first.
2. **Believing a caller that is not there.** A missing middle frame is a leaf without a frame pointer (50 of 1,000 stacks had `find_slow` with frame pointers, 964 with DWARF). Check unwinding before blaming a function.
3. **Profiling a `-O0` build** or a build without `-g`: the hot spots differ from the optimised program, or you get addresses.
4. **One run, one number.** The VM's run times varied 0.34 to 0.56 s for the same program. Repeat (`perf stat -r`), report a range, compare only with the same method.
5. **Reading self time as the thing to fix.** The callee at the top (`strcmp`) is the symptom; the caller that makes 4,000 calls per word (`find_slow`) is the fix. Use inclusive time and the stack.
6. **A sampling frequency of exactly 100 or 1,000 Hz** or so high that the profiler dominates (49,999 Hz cost 3 to 7 times the run time here). Use 99 or 997 for lockstep avoidance and a few hundred to a few thousand samples.
7. **Reading the flame graph x axis as time.** It is the sample population sorted alphabetically; left to right order means nothing.
8. **Trusting skid.** A heavy `add` after a `call` is the call. Look at the loop, not one instruction; use precise events on a PMU.
9. **Running `strace` on a live service.** 330 to 370 times slower on a call heavy loop (section 7). Use `perf trace -e one_call`, tracepoint sampling or eBPF.
10. **Reading an interpreter's profile as the application's.** Plain Python showed no `py::inner` frame; use `-X perf` (3.12 and later) or a language profiler.
11. **Fixing what is wide but not on the critical path** (a background thread) and **not re-measuring** the end to end number after the fix.
12. **Concluding from a VM without a PMU** that the cache is fine because no cache event was shown: it was not measured.

---

## 13. Questions out loud

1. **What does `perf stat` tell you that `top` does not?** Exact counts for a run: task-clock against elapsed (CPU bound or not), page faults, context switches and, with a PMU, instructions, cycles, IPC, cache and branch misses; and `-r` gives a spread.
2. **Counting, sampling and tracing?** Counting reads totals at near zero cost; sampling interrupts at a rate and shows where the time is, statistically; tracing records every event and gives exact sequences but costs per event.
3. **How does `perf record` work?** A timer or counter overflow interrupt records the instruction pointer (and the stack) at F Hz or every N events; `perf report` aggregates the samples by symbol.
4. **Self against children time?** Self is samples in the function's own instructions; children (inclusive) adds everything it called. Rank by self for the hot instruction, by inclusive for the function to change.
5. **Why can a stack be missing a frame?** Frame pointer unwinding follows saved `%rbp`; a leaf routine that does not set one (libc `strcmp`, `memcpy`) makes the walk skip its caller. Build with `-fno-omit-frame-pointer`, use DWARF (costly: 72 times the data here) or LBR.
6. **How do you read a flame graph?** Width is share of samples, height is stack depth, order is alphabetical. Find wide plateaus at the top for self time, then follow the stack down to the code you control.
7. **How do you compare two profiles?** A differential flame graph from the two folded files (red more, blue less), normalised by total samples, together with `perf stat -r` timings.
8. **The service is slow but the CPU profile looks fine?** It is probably waiting. Compare wall time with CPU time; do an off-CPU profile with `sched:sched_switch` (the stacks at blocking, time to return), look at `S` against `D` state, and at `perf trace -s` latencies.
9. **What does `perf sched` add?** Scheduling delay (runnable but waiting for a CPU), wait time and run time per task: it separates CPU saturation from blocking.
10. **How do you find false sharing?** Threads that scale badly with stores to shared structures; confirm with `perf c2c` (HITM by cache line, needs a PMU); fix with padding or `alignas(64)`.
11. **What changes in a VM?** Hardware counters may be missing (here `<not supported>`), so there is no IPC or miss data and no `c2c` or `mem`; software clock sampling, stacks, `perf trace` and `perf sched` still work. Use cachegrind or bare metal for counters.
12. **How do you prove a fix?** Fixed input, `perf stat -r` before and after, same output, a new profile (flat now?), and the differential graph: here 0.73 to 0.93 s down to 0.0176 s.

---

## 14. Problems

**Problem A. Read a flat profile.** The slow word counter's profile has `__strcmp_evex` at 78.1 percent self, `strcmp@plt` at 16.6 percent, `find_slow` at 4.1 percent, and `count_words` at 100 percent inclusive and 0.07 percent self. What share of the time is spent in the `strcmp` path, what is the inclusive share of `find_slow`, which function would you change, and why is changing `strcmp` itself not the answer?

**Problem B. Amdahl and the fix.** A function takes 50, 90, 95 and 98.7 percent of the run time. What is the largest speedup from making it free in each case? The word counter went from 0.73 s to 0.0176 s: what fraction of the original time was removed, and what does that say about the profile of the new version?

**Problem C. Wall against CPU.** A request loop ran 20 requests in 0.706 s of wall time with 82.9 ms of CPU. The on-CPU profile says `compute` is 94 percent; the off-CPU profile says 605 ms under `usleep` and 18 ms under `fsync`. What fraction of the wall time is on CPU, sleeping and in `fsync`? What speedup would you get from making `compute` free, and from removing the 30 ms wait?

**Problem D. Samples and error.** At 999 Hz a 0.4 s run gives about 400 samples. A function shows a 5 percent share: what is the standard error and the 95 percent range? How many samples do you need to know a 5 percent share to within 0.5 points, and how long is that at 999 Hz?

**Problem E. Pick the method.** For each case choose the tool and the output you would read: (1) a daemon uses 90 percent CPU and you need the hot function; (2) the same daemon is slow but at 5 percent CPU; (3) `perf report` shows `strcmp` as the hot function but the caller you expected is missing from the stack; (4) you need the latency of every `fsync` of a process without slowing it much; (5) a Python service is slow and the profile shows only interpreter functions; (6) `perf stat -e cycles` prints `<not supported>` and you need to compare two versions of a function.

---

## 15. Answers

**Problem A.**

```text
--- A: the slow profile, self against inclusive (numbers from the x2 run)
self time in the strcmp path (libc strcmp + its PLT stub): 94.70%
self time in find_slow itself:                          4.11%
find_slow inclusive (it calls strcmp):                  98.81%
```

- The `strcmp` path is 78.10 + 16.60 = **94.70 percent** (the library function and its PLT stub). `find_slow`'s inclusive share is **98.81 percent** (4.11 + 94.70): it is the caller that makes all of those calls.
- Change **`find_slow`** (the algorithm): it calls `strcmp` for each of the up to 4,000 table entries for each of 200,000 words. A faster `strcmp` can give a constant factor at best; a hash table makes the number of calls per word about 1 (measured: **86 times fewer instructions** in the cachegrind comparison and **about 40 times less time**).

**Problem B.**

```text
--- B: how much faster can fixing one function make the program? (Amdahl: speedup = 1 / (1 - fraction removed))
fraction of time removed 0.500 -> speedup 2.0x
fraction of time removed 0.900 -> speedup 10.0x
fraction of time removed 0.950 -> speedup 20.0x
fraction of time removed 0.987 -> speedup 76.9x
fraction of time removed 1.000 -> speedup unbounded
measured: slow 0.730 s, fast 0.0176 s, speedup 41x, so fraction removed 0.976
```

- Speedup limit 1 / (1 - p): **2.0x** at 50 percent, **10x** at 90, **20x** at 95 and **76.9x** at 98.7 percent.
- 0.73 s to 0.0176 s removed **97.6 percent** of the time (a **41 times** speedup, inside the 40 to 50 range of the runs). The new profile is **flat**: the largest stacks are `strcmp` (36 samples), `snprintf` building the test words (29) and `find_fast` (20) out of 122: no single function now dominates, so the next 2 times will cost far more effort than the first 40 times did (and the word generator, not part of the real work, now shows up).

**Problem C.**

```text
--- C: the request loop of section 6, on CPU against off CPU
on CPU 12%, sleeping 86%, fsync 3%, other 0%
if compute() were free: wall 0.623 s, speedup 1.13x
if the 30 ms wait were removed: wall 0.101 s, speedup 7.0x
```

- On CPU **12 percent**, in `usleep` **86 percent**, in `fsync` **3 percent**, other about 0.
- Making `compute` free: wall 0.623 s, **1.13 times**. Removing the 30 ms wait: wall 0.101 s, **7.0 times**. The CPU profile was right about the CPU and pointed at the wrong target for latency. (Check also `fsync`: 18 ms in total, 0.86 ms on average and 1.7 ms at worst, which matters for the tail rather than the mean.)

**Problem D.**

```text
--- D: how many samples does a share need?
n=400 samples, share 5%: standard error 1.09 points, 95% range about 2.9% to 7.1%
to know a 5% share within +-0.5 points: about 7300 samples (7 s at 999 Hz)
to know a 1% share within +-0.5 points: about 1522 samples (2 s at 999 Hz)
```

- The standard error is sqrt(p(1-p)/n) = **1.09 points**, so the 95 percent range is about **2.9 to 7.1 percent**: a 5 percent function in a 400 sample profile might really be 3 or 7 percent.
- Within 0.5 points: n = 1.96^2 x p(1-p) / 0.005^2 = about **7,300 samples**, **7 s at 999 Hz** (a 1 percent share needs about 1,500 samples). More samples shrink the error with the square root, so doubling the precision costs four times the samples. Profile for longer, not at a much higher frequency, when you need precision.

**Problem E.**

1. **`perf record -F 99 -g -p PID -- sleep 30`** then `perf report --no-children` and a flame graph. Read the widest top plateau, then its callers.
2. **Off-CPU**: `perf stat -p PID` (low `CPUs utilized`), then `perf record -a -e sched:sched_switch -g -- sleep 10` and the blocked time per stack, with `perf trace -s -p PID` for system call latencies and `perf sched timehist` for wait time against scheduling delay.
3. **A leaf without a frame pointer.** Re-record with `--call-graph dwarf` (or rebuild with `-fno-omit-frame-pointer`, or use LBR on a PMU). Measured: `find_slow` in 964 of 974 stacks with DWARF against 50 of about 1,000 with frame pointers.
4. **`perf trace -e fsync -p PID`** (or `perf record -e syscalls:sys_enter_fsync,syscalls:sys_exit_fsync`), not `strace`, which was about 330 to 370 times slower on a call heavy loop and also slows the call being measured.
5. **`python3 -X perf`** (3.12 and later) so that the perf map names the Python functions, then the folded stacks show `py::outer py::inner`; or a language profiler (`py-spy`, from memory).
6. **cachegrind** (`valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes`) on the same input for both versions: instructions, simulated cache misses and mispredicts (here 1.449 billion against 16.8 million instructions); then confirm on hardware that has a PMU.

---

## 16. Cheat sheet

```text
COUNT       perf stat -r 5 -e task-clock,page-faults,context-switches CMD   [-p PID] [-a sleep N]
            hardware: cycles instructions cache-misses branch-misses  (not supported in this VM)
            IPC = instructions / cycles ; multiplexing scales if events > counters
SAMPLE      perf record -F 99|997|999 -g CMD | -p PID -- sleep 30 | -a -g -- sleep 10
            --call-graph fp|dwarf|lbr ; -e EVENT -c N ; -F avoids exactly 100 and 1000
            perf report --no-children|--children --stdio -g none --sort sym|dso|comm --comm NAME
            perf annotate --stdio -s FUNC ; perf top ; perf script | collapse | flame graph
STACKS      frame pointer: -fno-omit-frame-pointer (leaf libc routines skip a frame)
            dwarf: complete but 70x the data ; lbr: PMU only ; python3 -X perf names Python frames
FLAME GRAPH width = samples (not time), height = depth, order alphabetical; read plateaus on top;
            folded line "a;b;c COUNT" ; differential: new coloured by change, normalised by totals
OFF CPU     perf record -a -e sched:sched_switch -g CMD ; pair out and in per task ; S timer or lock, D I/O
            on CPU 12% + off CPU 88% explained 706 ms of wall time
TRACE       perf trace -s CMD | -e SYSCALL -p PID ; perf sched record/latency/timehist
            strace -f is 330 to 370 times slower on a syscall heavy loop
KERNEL      perf record -a -g ; [k] symbols need kallsyms ; sys time high: look at the kernel stack
EVENTS      -e page-faults -c 1 ; -e context-switches ; -e 'syscalls:sys_enter_*' ; perf list
SHARING     perf c2c record/report (HITM per line) ; perf mem ; fix with alignas(64) or per thread data
NO PMU      software clock sampling works ; cachegrind simulates instructions, caches, branches
FIX LOOP    target -> triage (LX 05) -> baseline perf stat -r -> profile -> reason -> change one thing
            -> re-measure + same output -> re-profile (flat = done) -> keep evidence
NOT RUN HERE: hardware counters, IPC, top-down, precise events, LBR, perf mem, perf c2c, kprobes, eBPF tools
```

---

## 17. Sources

- `perf(1)` and its sub pages `perf-stat(1)`, `perf-record(1)`, `perf-report(1)`, `perf-annotate(1)`, `perf-script(1)`, `perf-trace(1)`, `perf-sched(1)`, `perf-c2c(1)`, `perf-mem(1)`: options, events, call graph modes. <https://man7.org/linux/man-pages/man1/perf.1.html>
- The Linux perf wiki: tutorial, examples and the event list. <https://perfwiki.github.io/main/>
- Brendan Gregg, *Systems Performance*, 2nd edition (2020): the USE method, CPU and off-CPU analysis, flame graphs, tracing costs. And his flame graph and off-CPU analysis pages. <https://www.brendangregg.com/flamegraphs.html> and <https://www.brendangregg.com/offcpuanalysis.html>
- Brendan Gregg, *The Flame Graph*, ACM Queue 14(2), 2016: the definition of the x and y axes and the folded stack format.
- Linux kernel documentation on perf events, and `perf_event_open(2)`: sampling, `perf_event_paranoid`, precise events and skid. <https://man7.org/linux/man-pages/man2/perf_event_open.2.html>
- Intel and AMD optimisation and PMU documentation (top-down method, PEBS, IBS, LBR), for the hardware counter parts quoted from memory. Yasin, *A Top-Down Method for Performance Analysis and Counters Architecture* (ISPASS 2014).
- Python documentation, *Python support for the Linux perf profiler* (`-X perf`, 3.12 and later). <https://docs.python.org/3/howto/perf_profiling.html>
- Valgrind manual, *Cachegrind: a cache and branch-prediction profiler*. <https://valgrind.org/docs/manual/cg-manual.html>
- Amdahl, *Validity of the single processor approach to achieving large scale computing capabilities* (1967), for the bound used in Problem B.
- This plan: [LX 05](05_performance_methodology_and_triage.md) for triage, [LX 03](03_processes_signals_and_proc.md) for `/proc`, [LX 06](06_memory_swap_and_the_oom_killer.md) for page faults, the [computer organization track](../04_computer_organization/00_plan.md) for caches, pipelines and coherence.
- Measurements: the scripts `x1` to `x5` and `ans_prob` of this session, Linux 6.18.44, `perf` 6.8.12, one VM without a PMU, as stated in the honest limits.
