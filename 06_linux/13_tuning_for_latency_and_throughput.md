# LX 13: Tuning for latency and throughput

*Part of the [Linux plan](00_plan.md) · Time: about 8 hours · You need: [LX 03](03_processes_signals_and_proc.md) (scheduling classes, `taskset`, `chrt`), [LX 05](05_performance_methodology_and_triage.md) (percentiles, active benchmarking), [LX 06](06_memory_swap_and_the_oom_killer.md) (huge pages, NUMA in outline), [LX 08](08_linux_networking_toolbox.md) (`ethtool`, `ss`, `sysctl net.*`) and [LX 09](09_containers_namespaces_and_cgroups.md) (cgroups and `cpuset`). The earlier tutorials found where time goes. This one **changes the machine so that less time is lost, and proves the change helped**.*

Tuning is the least glamorous and most misused part of systems work. Two sentences hold most of it. **Latency tuning removes sources of delay from one path; throughput tuning removes per-unit overhead from a stream.** And **every knob buys one thing by paying for another**, so a tuning you cannot measure, and whose price you cannot state, is a superstition. The tutorial follows the order a careful engineer works in: the method and a small measuring tool (section 1), how to read a latency distribution (2), where a thread runs (3), whether its CPU sleeps (4), what priority it has (5), who interrupts it (6), what its memory costs (7), the network and VM sysctls (8), an audit script (9), a worked recipe (10), then costs, mistakes, questions and problems.

**Nine honest limits, stated first.**

1. Everything is measured on **one Linux 6.18 VM** (Firecracker on a KVM host, **2 vCPUs, one NUMA node, 8 GB**, an Intel Xeon at 2.1 GHz), as **root**. The host's scheduler sits under every number here, so **the tails (p99.9 and max) include time the host took away and no setting inside the VM can remove it**. Only large ratios and clean orderings are trusted.
2. **The kernel command line cannot be changed**, so `isolcpus=`, `nohz_full=`, `rcu_nocbs=`, `irqaffinity=` and `preempt=` were **not run**. The kernel here is built with `CONFIG_NO_HZ_IDLE` (not `NO_HZ_FULL`), `CONFIG_HZ=250` and the **`none`** preemption model. Those sections are from memory and marked so.
3. **IRQ affinity cannot be written**: `echo 0 > /proc/irq/36/smp_affinity_list` fails with `Operation not permitted` (section 6). Steering interrupts is described, not demonstrated.
4. **There is no CPU frequency or C-state control**: no `cpufreq` interface and no `cpuidle` driver (`current_driver` reads `none`). Governors, `intel_pstate` and C-state limits are from memory. What **is** measurable is the cost of the virtual CPU halting (section 4).
5. **There is one NUMA node**, so remote-memory cost, `numactl --membind` effects and `numa_balancing` are described from memory (section 7).
6. **There is no `PREEMPT_RT` kernel, no `cyclictest` and no `tuned`.** The small program `wake` below does what `cyclictest` does in the one way needed. `PREEMPT_RT` and `tuned` profiles are from memory.
7. The **`cpuset` controller is the version 1 one** on this VM (`/sys/fs/cgroup/cpuset`), because the image mounts a hybrid hierarchy. Version 2 `cpuset` partitions are described, not run.
8. The **loopback** interface carries all the network tests (`iperf3` to `127.0.0.1`); there is no NIC with several queues, so **RSS and hardware offload effects are not measured**. RPS is measured, with a result that surprised me (section 6).
9. Two experiments **starved other tasks on purpose** (a `SCHED_FIFO` spinner on CPU 1, section 5). Each ran for seconds, was killed from CPU 0, and the sandbox was checked clean afterwards.

By the end you can:

- say **what latency and throughput tuning each change** and list the **eleven sources of delay** on a path (measured: the idle VM wakes a sleeping thread with a **median of 99 to 109 µs**, and **1,493 to 1,835 of 3,000 wake-ups are later than 100 µs**);
- measure a **wake-up latency distribution** with a 40 line program and **report it correctly**: percentiles, the count over a threshold, and **repeated runs** (measured: three identical runs gave a p99.9 of **579, 1,037 and 1,367 µs**);
- place a thread with **`taskset`, `sched_setaffinity` and `cpuset`** and say why a fence beats a pin (measured: next to three roaming burners a `taskset` thread had a **p99 of 3,037, 3,062 and 9,483 µs**; inside a `cpuset` fence it was **216 to 239 µs**);
- explain **idle-exit latency** and how to remove it (measured: a spinner that keeps the CPU from halting cut the median from **97 to 106 µs down to 66 to 69 µs** and the wake-ups over 100 µs from **1,371 to 1,780 down to 33 to 46** per 3,000);
- use **nice, `SCHED_FIFO` and the real-time throttle** and state the hazards (measured: `SCHED_FIFO 50` gave a median of **14 to 17 µs**; a FIFO spinner left a normal task **5.3 percent** of a CPU, starved a lower FIFO task for **3.5 seconds**, and made even a **higher-priority FIFO task wait up to 45 ms**);
- remove **timer slack** and say what it costs (measured: **50 µs to 1 ns** moved the median from **104 to 107 µs down to 53 to 68 µs**);
- read **`/proc/interrupts` and `/proc/softirqs`**, explain **RSS, RPS, RFS and XPS**, and know when RPS **hurts** (measured: on loopback with two CPUs, RPS **cut throughput from about 47 to about 31 Gbit/s**);
- choose **huge pages** with numbers (measured: a **2 GB** random pointer chase fell from **254 to 267 ns** per load to **167 to 174 ns** with 2 MB pages, about **34 percent**; at 4 MB there was **no difference**);
- do the **bandwidth-delay arithmetic** for socket buffers and read **autotuning** (measured: the receive buffer of a live connection grew from **131,072 to 1,864,135 bytes**);
- **audit a host** for latency settings with a short script, **write a boot command line** for an isolated-core machine, and **prove that a change helped** instead of believing it.

**Where the numbers come from.** Eleven experiment scripts (`x2` to `x11`; there is no `x1`), a problem answer script (`ans_prob`), three small C programs (`wake.c`, `jitter.c`, `tlb.c`) and one checker (`lcheck.sh`) were run in this session on the VM above. Every output in the tutorial is a frozen transcript of one of them. Process ids were replaced by `N` where they changed. A line such as `normal, run 1  min 58  p50 66 ...` is the output of `wake`: all times are **microseconds late**. Numbers not from these runs are marked "from memory" or come from a source at the end.

**Data and timing.** No data sets. Each script runs in seconds to a minute; the whole set takes about eight minutes. Most results are **three runs side by side** because a single run on a shared host proves nothing.

---

## 1. What tuning is, and a tool to measure it

### 1.1 Latency, throughput, and the price of each

| | **Latency tuning** | **Throughput tuning** |
| --- | --- | --- |
| Question | how long from event to reaction? | how many events per second, per core? |
| Cares about | the **tail**: p99, p99.9, max | the **average**, and CPU left over |
| Typical change | **remove** things from the path: sharing, interrupts, sleeping, page faults | **batch** and **amortise**: larger buffers, offloads, interrupt coalescing |
| Typical price | **idle CPUs burning power**, fewer cores for everything else, more operations work | **added delay** per unit (waiting for a batch), more memory |
| Fails by | one rare long delay that ruins the tail | saturation: a queue grows without bound |

They pull against each other. **Batching raises throughput by making each item wait for its batch; polling lowers latency by wasting a core.** A router that forwards 100 Gbit/s uses large rings, GRO and several queues; a trading gateway that answers in 5 µs pins one thread to one core and **polls** (busy waits, never sleeps). The same NIC can serve both with different settings. The first job is to **write down which one you are tuning for**, with a number: "p99.9 under 200 µs" or "9 Gbit/s per connection at 30 ms".

### 1.2 Eleven sources of delay on one path

A thread that must react to an event can be late for these reasons. The sections that follow take them in this order.

| # | Source | Typical size (from memory unless measured) | Section |
| --- | --- | --- | --- |
| 1 | Another task holds the CPU | up to a time slice, **milliseconds** | 3, 5 |
| 2 | The CPU was **halted** and must wake | **tens of µs** in a VM (measured), up to **100+ µs** deep C-states | 4 |
| 3 | **Timer slack**: the kernel may delay a normal task's timer | **50 µs** by default (measured) | 2.3 |
| 4 | The CPU runs **slower** than its top speed | frequency ramp, **ms** | 4 |
| 5 | An **interrupt or softirq** runs on your CPU | µs to ms | 6 |
| 6 | The scheduler **tick** (`HZ=250` here, 4 ms) | a few µs each | 3 |
| 7 | **Page faults, reclaim, compaction** | µs to ms | 7 |
| 8 | **TLB and cache misses** | tens to hundreds of ns each | 7 |
| 9 | **Remote NUMA memory** | up to 2 times a local access | 7 |
| 10 | **Real-time throttle** and priority inversion | **tens of ms** (measured) | 5 |
| 11 | **The host**: SMIs, a VM's vCPU being descheduled | invisible, large | 2 |

Items 2, 3, 6 and 11 are the ones people do not know they have.

### 1.3 The method

1. **State the goal as a percentile** (section 2.1).
2. **Measure before changing anything**, three runs at least, under the load the system really carries. A latency test on an idle machine measures an idle machine.
3. **Change one thing**, re-measure the same way, compare the **distribution** and the **spread between runs**.
4. **Write down what it costs** and who pays it.
5. **Keep a way back**: a boot entry, a script that undoes it, a timer that reverts a risky change.
6. **Repeat for a long time** at the end: a tail you can see in 3,000 samples may hide a worse one at 3 million.

### 1.4 The measuring tool: `wake`

The tool is the one idea behind `cyclictest`, in about 40 lines: ask the kernel to wake you at a **known absolute time**, read the clock when you wake, and record **how late** you were. Every source in the table above shows up as lateness. Here is the whole program (`wake.c`; build with `gcc -O2 -o wake wake.c`):

```c
/* wake.c: how late does a sleeping thread wake? A 1 ms periodic timer, like cyclictest.
   usage: wake [-n samples] [-a cpu] [-f fifo_prio] [-i interval_us] label */
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static int cmp(const void *a, const void *b) { long x = *(const long *)a, y = *(const long *)b; return (x > y) - (x < y); }

int main(int argc, char **argv) {
    int n = 3000, cpu = -1, prio = 0, c;
    long interval_us = 1000;
    while ((c = getopt(argc, argv, "n:a:f:i:")) != -1) {
        if (c == 'n') n = atoi(optarg);
        if (c == 'a') cpu = atoi(optarg);
        if (c == 'f') prio = atoi(optarg);
        if (c == 'i') interval_us = atol(optarg);
    }
    if (cpu >= 0) {
        cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu, &s);
        if (sched_setaffinity(0, sizeof s, &s)) { perror("affinity"); return 1; }
    }
    if (prio) {
        struct sched_param p = { .sched_priority = prio };
        if (sched_setscheduler(0, SCHED_FIFO, &p)) { perror("fifo"); return 1; }
    }
    long *late = malloc(n * sizeof *late);
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    for (int i = 0; i < n; i++) {
        next.tv_nsec += interval_us * 1000;
        while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        late[i] = (now.tv_sec - next.tv_sec) * 1000000000L + (now.tv_nsec - next.tv_nsec);
    }
    int over = 0;
    for (int i = 0; i < n; i++) if (late[i] > 100000) over++;
    qsort(late, n, sizeof *late, cmp);
    printf("%-28s min %5ld  p50 %5ld  p99 %6ld  p99.9 %6ld  max %7ld  over100us %d\n", argc > optind ? argv[optind] : "",
           late[0] / 1000, late[n / 2] / 1000, late[n * 99 / 100] / 1000, late[n * 999 / 1000] / 1000, late[n - 1] / 1000, over);
    return 0;
}
```

Three details decide whether the number means anything. **`TIMER_ABSTIME`** with a target advanced by a fixed step keeps **missed wake-ups from accumulating** (a late wake-up does not push the next one later). The **array of every sample** is sorted afterwards, so the percentiles are exact. And the program prints the **count over 100 µs**, because the median can look fine while a fifth of the wake-ups are slow. Options choose the CPU (`-a`) and a `SCHED_FIFO` priority (`-f`).

A second program, `jitter`, measures a different thing: it **spins reading the clock** and records every gap longer than 50 µs, which is time the CPU was **taken from the thread**, with no sleeping involved (`jitter.c`):

```c
/* jitter.c: spin reading the clock and report every gap longer than 50 us: time stolen from this CPU.
   usage: jitter [-a cpu] [-f fifo_prio] seconds */
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static long now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000L + t.tv_nsec; }

int main(int argc, char **argv) {
    int cpu = -1, prio = 0, c;
    while ((c = getopt(argc, argv, "a:f:")) != -1) { if (c == 'a') cpu = atoi(optarg); if (c == 'f') prio = atoi(optarg); }
    double secs = argc > optind ? atof(argv[optind]) : 3;
    if (cpu >= 0) { cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu, &s); sched_setaffinity(0, sizeof s, &s); }
    if (prio) { struct sched_param p = { .sched_priority = prio }; sched_setscheduler(0, SCHED_FIFO, &p); }
    long start = now_ns(), prev = start, end = start + (long)(secs * 1e9), max = 0, total = 0;
    long at[8]; int n50 = 0, n1000 = 0, shown = 0;
    long cur;
    while ((cur = now_ns()) < end) {
        long gap = cur - prev;
        if (gap > 50000) {
            n50++; total += gap;
            if (gap > 1000000) n1000++;
            if (gap > max) max = gap;
            if (shown < 8) at[shown++] = cur - start;
        }
        prev = cur;
    }
    printf("gaps>50us %d  gaps>1ms %d  worst %ld us  stolen %.1f ms of %.0f ms (%.2f%%)\n", n50, n1000, max / 1000,
           total / 1e6, secs * 1000, 100.0 * total / (secs * 1e9));
    printf("first gaps at (ms):");
    for (int i = 0; i < shown; i++) printf(" %.1f", at[i] / 1e6);
    printf("\n");
    return 0;
}
```

`wake` answers "how late do I wake"; `jitter` answers "how much of my CPU time is stolen". A polling design cares about the second.

---

## 2. Reading a latency distribution

### 2.1 What to report

A latency is a **distribution**, never a number. Report **p50** (the typical case), **p99 and p99.9** (the tail users and timeouts feel), the **maximum** (one bad event), and **how often a threshold is crossed**. A goal such as "p99.9 under 200 µs" can be checked; "low latency" cannot. The average hides everything: one 3 ms stall in 3,000 samples moves the mean by 1 µs.

### 2.2 The idle machine, three runs

```text
--- 1. idle machine, three runs
idle, normal, run 1          min    66  p50    99  p99    227  p99.9    579  max     972  over100us 1493
idle, normal, run 2          min    63  p50   109  p99    200  p99.9   1037  max    2013  over100us 1833
idle, normal, run 3          min    67  p50   108  p99    237  p99.9   1367  max    3315  over100us 1835
idle, SCHED_FIFO 50          min    23  p50    57  p99    159  p99.9    448  max    1015  over100us 333
```

Read it. Nothing else is running and the median is **99 to 109 µs**, much worse than the few microseconds a bare-metal machine gives (from memory: 5 to 30 µs on a tuned server). Two reasons appear later: the virtual CPU must wake from a halt (section 4) and the default timer slack (section 2.3). The p99.9 is **579, 1,037 and 1,367 µs** in three identical runs, and the maximum **972, 2,013 and 3,315 µs**. **The tail varies by a factor of two or three between identical runs**, so one run cannot rank two settings by their p99.9; that is why every table in this tutorial has runs side by side, and why a claim needs a **gap larger than the spread**. The last line, a `SCHED_FIFO` thread on the same idle machine, has a median of **57 µs**: priority does not remove the halted-CPU cost, but it removes the slack (next section).

### 2.3 Timer slack: lateness allowed on purpose

A normal (`SCHED_OTHER`) task's timers get **slack**: the kernel may fire them up to `timerslack_ns` late so that nearby timers can share one wake-up and save power. The default is **50,000 ns**, and it is per process, in `/proc/PID/timerslack_ns`. `SCHED_FIFO`, `SCHED_RR` and `SCHED_DEADLINE` tasks have **no slack**. Setting it to 1 ns for the test shell:

```text
--- 1. timer slack: the default allows a normal task's wake-up to be late on purpose
$ cat /proc/self/timerslack_ns
50000
slack 50 us (default), run 1 min    72  p50   104  p99    261  p99.9   1814  max    2875  over100us 1661
slack 50 us (default), run 2 min    74  p50   105  p99    233  p99.9    709  max    1683  over100us 1806
slack 50 us (default), run 3 min    73  p50   107  p99    373  p99.9   2353  max    2528  over100us 1768
$ cat /proc/self/timerslack_ns
1
slack 1 ns, run 1            min    16  p50    68  p99    252  p99.9   2392  max    4347  over100us 625
slack 1 ns, run 2            min    14  p50    59  p99    202  p99.9   1166  max    1995  over100us 411
slack 1 ns, run 3            min    22  p50    53  p99    384  p99.9   8385  max   10333  over100us 357
```

The median fell from **104, 105 and 107 µs to 68, 59 and 53 µs**, and the wake-ups over 100 µs from **1,661, 1,806 and 1,768 to 625, 411 and 357**. The tail did not improve (p99.9 of 8,385 µs in the last run is the host). A program does this with `prctl(PR_SET_TIMERSLACK, 1)`. **The price is power and timer coalescing**: every timer now wakes the CPU on its own, so do it for the threads that need it, not for the machine. Part of the 40 to 50 µs gap between the idle and the busy medians in section 4 is the same effect and part is the halt.

---

## 3. Where a thread runs: affinity, fences and the tick

### 3.1 Pin, and what pinning does and does not do

`taskset -c 1 CMD` or `sched_setaffinity` limits a thread to a set of CPUs. It gives **cache and TLB warmth** (the thread's data stays in one core's caches) and **predictability**. Measured effect on migrations: a burner competing with two others was moved **11 times** in 3 seconds when free, and **1 time** (the initial placement) when pinned:

```text
--- 5. scheduler migrations of one burner while two others compete
$ perf stat -e cpu-migrations,context-switches timeout 3 sh -c 'while :; do :; done' 2>&1 | grep -E 'migrations|context'
                11      cpu-migrations
               301      context-switches
$ perf stat -e cpu-migrations,context-switches taskset -c 1 timeout 3 sh -c 'while :; do :; done' 2>&1 | grep -E 'migrations|context'
                 1      cpu-migrations
               269      context-switches
```

The context switches (301 against 269) barely differ: **pinning stops the moves, not the sharing**. And a pin protects **only your side**: other tasks may still run on your CPU. Section 3.2 shows what that costs.

### 3.2 What stolen CPU time looks like

```text
--- 1. a spinning thread on CPU 1, idle machine
gaps>50us 35  gaps>1ms 1  worst 1151 us  stolen 5.0 ms of 3000 ms (0.17%)
first gaps at (ms): 25.9 51.7 55.6 187.2 188.3 188.4 353.2 372.4
gaps>50us 158  gaps>1ms 3  worst 2069 us  stolen 34.7 ms of 3000 ms (1.16%)
first gaps at (ms): 76.7 104.5 139.5 144.5 167.5 172.5 207.4 334.4
--- 2. a normal burner shares CPU 1
gaps>50us 412  gaps>1ms 375  worst 4536 us  stolen 1512.3 ms of 3000 ms (50.41%)
first gaps at (ms): 8.0 16.0 24.0 32.0 40.0 48.0 56.0 64.0
```

On an idle CPU a spinning thread loses **0.17 and 1.16 percent** of its time to the host and to interrupts: **35 and 158 gaps over 50 µs** in three seconds, worst **1,151 and 2,069 µs**. That is the floor of this VM. When an ordinary burner shares the CPU the spinner loses **50.41 percent** (1,512 ms of 3,000 ms) in **375 gaps over 1 ms**, the first at **8.0, 16.0, 24.0, 32.0 ms**: a steady pattern of about **4 ms** lost every **8 ms**. With `CONFIG_HZ=250` a tick comes every 4 ms; I take the 4 ms rhythm to be the scheduler's switching at ticks, but I did not prove that separately.

```text
--- 4. burner on CPU 0, spinner pinned to CPU 1
gaps>50us 163  gaps>1ms 11  worst 1950 us  stolen 33.1 ms of 3000 ms (1.10%)
first gaps at (ms): 1.3 7.7 11.6 71.9 83.7 102.5 149.9 157.4
```

With the burner on **the other** CPU the spinner loses **1.10 percent**, close to the idle floor: **a different CPU is a different world** (this VM has no hyper-threads; on a machine with SMT the sibling would share execution units and caches, so "the other CPU" must be **a different core**, from memory).

### 3.3 A fence beats a pin: `cpuset`

`taskset` is a request each program makes about itself. A **`cpuset`** is a **fence the administrator builds** around a group: the kernel will never run those tasks outside their CPUs and memory nodes (LX 09). To protect a latency task, give it a cpuset **and give everything else a different one**.

```text
--- 1. two cpusets: lat owns CPU 1, bg owns CPU 0
$ grep . /sys/fs/cgroup/cpuset/lat/cpuset.cpus /sys/fs/cgroup/cpuset/lat/cpuset.mems /sys/fs/cgroup/cpuset/bg/cpuset.cpus /sys/fs/cgroup/cpuset/bg/cpuset.mems
/sys/fs/cgroup/cpuset/lat/cpuset.cpus:1
/sys/fs/cgroup/cpuset/lat/cpuset.mems:0
/sys/fs/cgroup/cpuset/bg/cpuset.cpus:0
/sys/fs/cgroup/cpuset/bg/cpuset.mems:0
```

The test: three burners roam freely and the latency task is **pinned with `taskset` to CPU 1**, then the same burners are put inside a cpuset that owns only CPU 0 and the latency task inside one that owns CPU 1.

```text
--- 2. three burners that are free to roam, and the latency task pinned with taskset
taskset to CPU 1, run 1      min    32  p50    68  p99   9483  p99.9  31022  max   32978  over100us 180
taskset to CPU 1, run 2      min    21  p50    69  p99   3062  p99.9   6944  max    7912  over100us 131
taskset to CPU 1, run 3      min    66  p50    69  p99   3037  p99.9   6993  max    7969  over100us 148
--- 3. the same burners, started inside bg
$ for p in N N N; do grep -H Cpus_allowed_list /proc/$p/status; done
/proc/N/status:Cpus_allowed_list:   0
/proc/N/status:Cpus_allowed_list:   0
/proc/N/status:Cpus_allowed_list:   0
lat cpuset, run 1            min    68  p50   125  p99    216  p99.9    975  max    1949  over100us 2238
lat cpuset, run 2            min    32  p50   117  p99    237  p99.9    991  max    1706  over100us 2044
lat cpuset, run 3            min    68  p50   117  p99    239  p99.9   1031  max    2118  over100us 2024
$ sh -c 'echo $$ > /sys/fs/cgroup/cpuset/bg/tasks; taskset -c 1 true; echo exit status $?'
taskset: failed to set pid N's affinity: Invalid argument
exit status 1
```

The pinned task has an excellent median (**68 to 69 µs**, because CPU 1 never halts) and a terrible tail: **p99 of 3,037, 3,062 and 9,483 µs**, **maximum 7.9 to 33.0 ms**, because burners are scheduled on CPU 1 too and the task waits for its turn. Inside the fence the p99 is **216, 237 and 239 µs** and the maximum **1.7 to 2.1 ms**: **more than ten times better at the 99th percentile**. The median got **worse** (**125, 117 and 117 µs**) because CPU 1 is now idle between wake-ups and must wake from a halt. That is the trade of the next section. Note the proof that the fence is real: the burners' `Cpus_allowed_list` reads `0`, and a task inside the fence that tries `taskset -c 1` is refused with `Invalid argument`.

**Moving things into the fence is the work.** Kernel threads cannot all be moved and every new process starts in its parent's cpuset, so a fence is only as good as the rule that puts the rest of the system **outside** it. Systemd does this with `AllowedCPUs=` on slices; Kubernetes' static CPU manager gives a guaranteed pod exclusive CPUs.

### 3.4 Boot-time isolation: `isolcpus`, `nohz_full`, `rcu_nocbs` (from memory)

A cpuset keeps **user tasks** off a CPU. It does not stop the **kernel's own** work: the scheduler tick, RCU callbacks, workqueues, timers, unbound kthreads, IRQs. Boot parameters take those away. They were **not run here**, because the command line cannot be changed; `lcheck.sh` in section 9 reports their state.

| Parameter | What it removes from the listed CPUs | Price |
| --- | --- | --- |
| `isolcpus=domain,managed_irq,2-15` | the CPUs leave the **scheduler's load balancing** (no task is placed there unless pinned) and **managed IRQs** are kept off them | you must pin work there yourself |
| `nohz_full=2-15` | the **periodic tick stops** when exactly one task runs (needs `CONFIG_NO_HZ_FULL`, **not built here**) | the housekeeping CPUs do more; user/kernel transitions cost more |
| `rcu_nocbs=2-15` | **RCU callbacks** run on other CPUs' threads | callback latency moves elsewhere |
| `irqaffinity=0-1` | the **default IRQ affinity** is the housekeeping CPUs | none of note |
| `rcu_nocb_poll`, `nosoftlockup`, `tsc=reliable`, `skew_tick=1` | extra sources: RCU wake-ups, watchdog, clocksource check, tick alignment | each loses a safety net |

On a machine so set up, a pinned spinner measured with `jitter` shows **almost no gaps**: the tick is gone and nothing else is allowed there (from memory; typical remaining gaps are a handful of µs from SMIs). **The idea is a core the kernel promised to leave alone.** Version 2 cpusets can do part of this at run time: an isolated **partition** (`cpuset.cpus.partition` set to `isolated`) removes its CPUs from load balancing without a reboot. The `nohz_full` and RCU parts still need the boot line.


---

## 4. Does the CPU sleep? Idle states and frequency

### 4.1 Idle-exit latency, measured

When a CPU has nothing to run, the kernel puts it to sleep (on x86, `HLT` or `MWAIT`). The next wake-up must **bring it back**. On bare metal that is the exit latency of a **C-state**; in a VM the `HLT` is a **VM exit** and the host must schedule the virtual CPU again. Either way the first instruction after the interrupt runs late. To see it, run `wake` pinned to CPU 1 while CPU 1 is idle, and then while a **`SCHED_IDLE` spinner** (the lowest class: it runs only when nothing else wants the CPU, so it never delays the real task) keeps CPU 1 awake:

```text
--- 1. wake-up latency on CPU 1 when CPU 1 is otherwise idle (three runs)
idle CPU 1, run 1            min    68  p50   106  p99    207  p99.9   2011  max    2695  over100us 1780
idle CPU 1, run 2            min    61  p50   101  p99    215  p99.9   1119  max    2083  over100us 1547
idle CPU 1, run 3            min    24  p50    97  p99    214  p99.9   1099  max    1772  over100us 1371
--- 2. the same, with a SCHED_IDLE spinner keeping CPU 1 from halting
spinner on CPU 1, run 1      min    65  p50    69  p99    142  p99.9   1543  max    1776  over100us 46
spinner on CPU 1, run 2      min    59  p50    66  p99    106  p99.9   2037  max    3990  over100us 33
spinner on CPU 1, run 3      min    23  p50    67  p99    107  p99.9    906  max    1186  over100us 44
```

The median fell from **106, 101 and 97 µs to 69, 66 and 67 µs**, and the wake-ups later than 100 µs from **1,780, 1,547 and 1,371 to 46, 33 and 44** out of 3,000: **a thirty to forty fold reduction in "slow" wake-ups from one line**. The tail (p99.9 of 906 to 2,037 µs) did not change; the host decides that. This is the same effect behind the odd result in section 3.3, where the pinned task on a busy CPU had a **better median** than the fenced task on an idle CPU.

**What it costs.** The CPU is now **100 percent busy** doing nothing: more power and heat, a **smaller turbo budget for the other cores** (from memory), a misleading utilisation graph and, on a hyper-threaded machine, a sibling that shares execution units with a spinner (use a `pause`-based loop or `mwait`). On a bare-metal latency server this is deliberate and the three standard forms are:

| Method | How | Notes (from memory) |
| --- | --- | --- |
| Limit C-states in the **kernel** | `processor.max_cstate=1 intel_idle.max_cstate=1`, or `idle=poll` (never sleeps at all) | `idle=poll` costs the full power of the chip at all times |
| Limit them **per run** | a program opens `/dev/cpu_dma_latency` and writes the latency it can tolerate in µs (a 32-bit integer); the kernel keeps C-states whose exit latency is longer than that out of use **while the file stays open** | the right tool for one process; closing the file releases the request |
| Limit them in **firmware** | disable C-states or choose a "maximum performance" profile in the BIOS | affects every OS on the machine |

The second row is the polite one, so I tried it: write a zero to `/dev/cpu_dma_latency` and hold it open, then run the same test.

```text
--- 3. the same, holding /dev/cpu_dma_latency at 0 (power-management QoS)
$ cat /sys/devices/system/cpu/cpuidle/current_driver /sys/devices/system/cpu/cpuidle/current_governor
none
menu
QoS 0, run 1                 min    68  p50   132  p99    215  p99.9    775  max    2055  over100us 2213
QoS 0, run 2                 min    71  p50   109  p99    213  p99.9    494  max    1303  over100us 1905
QoS 0, run 3                 min    17  p50    99  p99    257  p99.9    770  max    1746  over100us 1485
```

**It changed nothing here**: the medians were **132, 109 and 99 µs** and **2,213, 1,905 and 1,485** wake-ups were over 100 µs. The reason is on the line above the results: the **`cpuidle` driver is `none`**. A virtual CPU has no C-states to limit; its idle is one `HLT`, and the host owns what happens next. The interface exists and accepts the request; there is nothing for it to act on. **A tuning that is accepted and has no effect is the common failure**, which is why every change here is followed by a measurement. On bare metal with `intel_idle` the same request would keep the CPU out of the deep states.

### 4.2 Frequency

The other half of "the CPU is slow when you need it" is **frequency scaling**. On bare metal the policy lives in `/sys/devices/system/cpu/cpuN/cpufreq/`: the **governor** (`performance` holds the top frequency, `powersave` follows load slowly, `schedutil` follows the scheduler's utilisation, and with `intel_pstate` the hardware itself decides and the knob is `energy_performance_preference`), and the limits `scaling_min_freq` and `scaling_max_freq`. A core that was idle may take **milliseconds** to reach full speed, and **turbo** speeds depend on how many cores are active and on temperature, so the same code runs at different speeds in different minutes (all from memory). The latency settings are `performance` plus a **floor** at the maximum non-turbo frequency, and a check that **turbo and thermal throttling** are not undoing it (`turbostat`, `perf stat` cycles per second).

On this VM there is nothing to set:

```text
--- 4. what the host offers for frequency and idle states
$ ls /sys/devices/system/cpu/cpu0/cpufreq 2>&1 | head -3
ls: cannot access '/sys/devices/system/cpu/cpu0/cpufreq': No such file or directory
$ ls /sys/devices/system/cpu/cpu0/cpuidle 2>&1 | head -3
ls: cannot access '/sys/devices/system/cpu/cpu0/cpuidle': No such file or directory
$ ls /sys/devices/system/cpu/cpufreq /sys/devices/system/cpu/cpuidle
/sys/devices/system/cpu/cpufreq:

/sys/devices/system/cpu/cpuidle:
available_governors
current_driver
current_governor
current_governor_ro
```

The `cpufreq` and `cpuidle` directories under `cpu0` do not exist; the global `cpufreq` directory is empty and the global `cpuidle` directory has only the driver and governor files (driver `none`, governor `menu`). The host owns the clock. **When tuning in a cloud VM, you can pin and prioritise inside the guest, but frequency, C-states and the host scheduler are decided outside it**: a latency target below what the host provides is a request to your cloud provider, for a dedicated or bare-metal instance.

---

## 5. Priority: nice, real-time, and the throttle

### 5.1 The policies

| Policy | Picks by | Typical use | Danger |
| --- | --- | --- | --- |
| `SCHED_OTHER` (normal) | fair share by weight (`nice -20` to `19`) | everything | none: the default |
| `SCHED_BATCH` | like normal, treated as non-interactive | long computations | none |
| `SCHED_IDLE` | runs only if nothing else wants the CPU | background work, the section 4 spinner | starvation (by design) |
| `SCHED_FIFO` `1..99` | **highest priority runs until it blocks or yields**; same priority in arrival order | control loops, packet threads | **never yields: starves everything below it** |
| `SCHED_RR` `1..99` | like FIFO with a time slice among equals | as above | as above |
| `SCHED_DEADLINE` | earliest deadline first, with a declared runtime, deadline and period; the kernel **refuses** a set it cannot guarantee | periodic real-time jobs | misdeclared budgets are killed |

```text
--- 0. the policies and the throttle settings
$ chrt -m | head -3
SCHED_OTHER min/max priority    : 0/0
SCHED_FIFO min/max priority     : 1/99
SCHED_RR min/max priority       : 1/99
$ cat /proc/sys/kernel/sched_rt_period_us /proc/sys/kernel/sched_rt_runtime_us
1000000
950000
```

`chrt -m` lists the priority range of each policy (`SCHED_FIFO` and `SCHED_RR` take 1 to 99; the normal policy has only 0). The second command shows the real-time **throttle** values used in section 5.3.

### 5.2 What each buys, measured

Both CPUs were kept busy with one burner each, and the latency task ran unpinned so it had to **preempt** a burner to run:

```text
--- 2. both CPUs busy, one burner each
normal, run 1                min    58  p50    66  p99    162  p99.9   3930  max    3944  over100us 38
normal, run 2                min    59  p50    67  p99    538  p99.9   3752  max    3958  over100us 63
normal, run 3                min    61  p50    68  p99     95  p99.9    518  max    1497  over100us 20
nice -20                     min    59  p50    67  p99    120  p99.9   1574  max    1620  over100us 45
SCHED_FIFO 50, run 1         min     9  p50    17  p99     86  p99.9    592  max    2349  over100us 25
SCHED_FIFO 50, run 2         min     8  p50    17  p99     75  p99.9   1850  max    3024  over100us 26
SCHED_FIFO 50, run 3         min     8  p50    14  p99     82  p99.9   2935  max    3911  over100us 27
SCHED_FIFO 50 on CPU 1       min     8  p50    17  p99     56  p99.9   1390  max    2014  over100us 15
```

`nice -n -20` (the strongest normal weight) gave **no change in the median** (67 µs against 66 to 68) and a p99 of 120 against 95 to 538 µs: **inside the noise**. A normal task that wakes on a busy CPU is already placed quickly by the fair scheduler; the weight matters when it **keeps running** and competes, not at the instant of waking. `SCHED_FIFO 50` took the **median to 14 to 17 µs** (it preempts at once and has no timer slack) and the p99 to 75 to 86 µs. Its p99.9 and maximum (**592 to 2,935 µs** and **2.3 to 3.9 ms**) were **as bad as the normal task's**: the host's tail again. **Real-time priority fixes what the guest scheduler does; it cannot fix what the host does.**

The question "does it really help" needs more than one run per setting. Problem A repeats the comparison five times each, next to a burner on the same CPU, and shows the gap between settings to be larger than the spread between runs.

### 5.3 The real-time throttle, and the hazards

A `SCHED_FIFO` task that never blocks would lock a CPU for ever, including against the administrator who could kill it. The kernel's safety net is the **real-time throttle**: in each **period** (`sched_rt_period_us`, 1,000,000 µs) all real-time tasks on a CPU together may run for at most **`sched_rt_runtime_us`** (950,000 µs here), then are **blocked until the period ends**: **5 percent of every second belongs to everybody else**. Set the runtime to `-1` and the net is gone.

A `SCHED_FIFO 50` spinner on CPU 1 and three probes, each measured:

```text
--- 1. a FIFO 50 spinner owns CPU 1; who can still wake on time?
FIFO 60 (higher)             min     8  p50    17  p99  33145  p99.9  44157  max   45129  over100us 97
normal (SCHED_OTHER)         min    66  p50 71354  p99 215333  p99.9 217286  max  217286  over100us 240
after 4 s the FIFO 40 task has printed: 0 bytes
FIFO 40 (lower)              min 3516679  p50 3760344  p99 3998939  p99.9 4002814  max 4002814  over100us 500
```

Read the three lines.

- **A higher priority (FIFO 60) wakes fast and late.** Its median is **17 µs**, but its **p99 is 33,145 µs and its maximum 45,129 µs**. The throttle blocks **all** real-time tasks on the CPU, the higher-priority one too, for about 50 ms each second. **Priority orders the real-time tasks; it does not exempt one from the throttle.**
- **A normal task** got the CPU only in the throttle gaps: median **71 ms**, maximum **217 ms**, for a task that would wake in 70 µs on a free CPU.
- **A lower priority (FIFO 40) was starved.** After four seconds it had printed nothing; it ran only after the spinner was killed from CPU 0, and **every one of its wake-ups was between 3.5 and 4.0 seconds late**. The throttle gap goes to **non-real-time** tasks; a FIFO task below a busy one gets nothing, **and cannot even die**, because a `SIGKILL` is acted on only when the task next runs.

The second test asks how much of the CPU an ordinary task gets next to a FIFO spinner:

```text
--- 2. the throttle: how much CPU does a normal task get next to a FIFO 50 spinner?
normal task on CPU 1 got 32 ticks of CPU in 6 s (100 ticks = 1 s)
```

**32 ticks of 600: 5.3 percent**, the 5 percent the throttle reserves. The earlier jitter test shows the same event from the spinner's side: a FIFO 50 spinner with a normal burner on its CPU lost only **3.76 percent** of its time, but **2 gaps over 1 ms and the worst gap 54,008 µs**, the throttle at work:

```text
--- 3. the same, but the spinner is SCHED_FIFO 50
gaps>50us 44  gaps>1ms 2  worst 54008 us  stolen 112.7 ms of 3000 ms (3.76%)
first gaps at (ms): 143.7 217.6 335.7 498.7 728.7 753.7 820.2 1003.9
```

**Rules that follow.** Leave one CPU (CPU 0) for the administrator and non-real-time work, never pin a real-time spinner there, and run the **script that may kill it from that CPU** (these experiments did). Never set the throttle to `-1` on a machine you cannot reach by another route. Give real-time threads **`RLIMIT_RTTIME`** so the kernel kills one that runs too long without blocking. A real-time thread must **block** (on a timer, a futex or a socket) in every iteration. Cgroup v1's `cpu.rt_runtime_us` can budget real-time time per group (from memory).

### 5.4 Preemption models and `PREEMPT_RT` (from memory)

How soon can the kernel itself be interrupted when a higher-priority task becomes runnable while a task is inside a **system call**?

| Model | Meaning | Where |
| --- | --- | --- |
| `none` | the kernel is preempted only at explicit points; best throughput | **this VM** (`CONFIG_PREEMPT_DYNAMIC=y`, current `none`) |
| `voluntary` | more explicit points | older desktops |
| `full` (`PREEMPT`) | preempted almost anywhere except where a spinlock is held | desktops, low-latency builds |
| **`PREEMPT_RT`** | spinlocks become **sleeping** locks with **priority inheritance**, interrupt handlers run as **threads**, almost everything is preemptible | real-time systems; **mainline since Linux 6.12** |
| `lazy` | newer: full preemption for real-time tasks, lazy for fair ones | recent kernels |

On a kernel built with `PREEMPT_DYNAMIC` the model is chosen on the command line (`preempt=full`) or at run time through debugfs. Here `/sys/kernel/debug/sched/preempt` exists but **reading it is refused** (`Operation not permitted`), so I cannot show the switch. The price of `PREEMPT_RT` is **throughput** (more locking and context switches) and an ecosystem of drivers and modules that must be RT-clean. The benefit is a **bounded worst case**: on tuned bare metal, `cyclictest` worst-case latencies of tens of microseconds for RT against hundreds or more for a stock kernel are the figures usually quoted (from memory: measure on your own hardware, under stress, for hours). A real-time **program** also needs `mlockall(MCL_CURRENT | MCL_FUTURE)` so its pages are never faulted, a **pre-faulted stack and heap**, no allocation and no `printf` in the loop, **priority-inheritance mutexes** (`PTHREAD_PRIO_INHERIT`) so that a low-priority holder of a lock is boosted when a high-priority task waits for it (the remedy for priority inversion), and `SCHED_FIFO` or `SCHED_DEADLINE` set by the **administrator's policy**, not by whatever the program asks for.

---

## 6. Who interrupts you: IRQs, softirqs and network queues

### 6.1 Reading the interrupt tables

Every device interrupt is a line in `/proc/interrupts`, with a count per CPU, and the deferred half of the work (`softirq`: network receive, block completion, timers, RCU) is counted in `/proc/softirqs`. Both are the first place to look when a "quiet" core is not quiet.

```text
--- 5. interrupts and softirqs
$ grep -E 'virtio[0-9]-(req|input)' /proc/interrupts | head -4
 34:         93          0 PCI-MSIX-0000:00:0a.0   1-edge      virtio9-input
 36:          0     281888 PCI-MSIX-0000:00:02.0   1-edge      virtio1-req.0
 38:         58          0 PCI-MSIX-0000:00:03.0   1-edge      virtio2-req.0
 40:          0       1592 PCI-MSIX-0000:00:04.0   1-edge      virtio3-req.0
$ cat /proc/irq/36/smp_affinity_list
0-1
$ echo 0 > /proc/irq/36/smp_affinity_list
bash: echo: write error: Operation not permitted
$ pgrep -a irqbalance || echo irqbalance not running
irqbalance not running
$ grep -E '^ +(CPU|HI|TIMER|NET_RX|NET_TX|BLOCK|SCHED|RCU):' /proc/softirqs
          HI:          0          0
       TIMER:      71829      52183
      NET_TX:          5          9
      NET_RX:    1233237    1380425
       BLOCK:        112     283452
       SCHED:     175422     168744
         RCU:      50948      55395
```

Read it. The disk's request interrupt (`virtio1-req.0`, IRQ 36) delivered **281,888 interrupts, all on CPU 1**, and the `BLOCK` softirq shows **283,452 on CPU 1** against 112 on CPU 0: **the disk completions are handled on CPU 1**. If CPU 1 were the one you wanted quiet, this is the line that would explain its jitter. `NET_RX` has **1,233,237 and 1,380,425** (the loopback tests spread over both). The affinity is `0-1` (any CPU), and the **write that would change it is refused** here. On bare metal you would set `/proc/irq/N/smp_affinity_list`, or give the machine `irqaffinity=0-1` so that every IRQ **defaults** to the housekeeping CPUs, and run `irqbalance` only with the isolated CPUs on its **banned list** (`IRQBALANCE_BANNED_CPULIST`) or not at all. `irqbalance` is not running here. **Managed IRQs** (NVMe and modern NIC queues) are assigned by the kernel to the queue's CPUs and cannot be moved by hand; `isolcpus=managed_irq` keeps them off the isolated set when the queue can be served elsewhere (from memory).

### 6.2 Spreading network receive work

A fast NIC delivers more packets than one CPU can process, so the work is **spread**:

| Mechanism | Where | What it does |
| --- | --- | --- |
| **RSS** (receive-side scaling) | hardware | the NIC **hashes** each flow's 4-tuple to one of several **hardware queues**, each with its own interrupt and CPU; one flow stays on one queue so packets are not reordered |
| **RPS** (receive packet steering) | software | for a NIC with **one queue**, the first CPU hashes and **hands the packet to another CPU** with an IPI; set per queue in `rps_cpus` |
| **RFS** (receive flow steering) | software, with RPS | steers a flow to the CPU where the **consuming thread** last ran, so the data is cache-warm; `rps_sock_flow_entries`, `rps_flow_cnt` |
| **XPS** (transmit steering) | software | picks the **transmit queue** by sending CPU, avoiding lock sharing |
| **aRFS** | hardware | the NIC itself steers flows to the consumer's CPU |

This VM's NIC has **one queue each way** and a single combined channel:

```text
--- 1. the NIC queues and the RPS settings
$ ls /sys/class/net/eth0/queues /sys/class/net/lo/queues
/sys/class/net/eth0/queues:
rx-0
tx-0

/sys/class/net/lo/queues:
rx-0
tx-0
$ cat /sys/class/net/lo/queues/rx-0/rps_cpus /sys/class/net/lo/queues/rx-0/rps_flow_cnt /proc/sys/net/core/rps_sock_flow_entries
0
0
0
$ ethtool -l eth0 2>&1 | head -8
Channel parameters for eth0:
Pre-set maximums:
RX:             n/a
TX:             n/a
Other:          n/a
Combined:       1
Current hardware settings:
RX:             n/a
```

The directory lists `rx-0` and `tx-0` only, and `ethtool -l` reports **`Combined: 1`**: there is no RSS to tune. `rps_cpus` is `0` (off). Loopback offers a software test of RPS: four TCP streams to `127.0.0.1`, four seconds, with `rps_cpus` off and then set to `3` (both CPUs), counting the receive softirqs and the packets each CPU processed (from `/proc/net/softnet_stat`):

```text
--- 2. four TCP streams over loopback, four seconds, three runs each
rps_cpus=0 (off), run 1    44.9 Gbit/s  NET_RX softirqs  cpu0   177008  cpu1   194455   packets processed  cpu0   227881  cpu1   239543
rps_cpus=0 (off), run 2    46.4 Gbit/s  NET_RX softirqs  cpu0   176373  cpu1   173917   packets processed  cpu0   226819  cpu1   228016
rps_cpus=0 (off), run 3    48.9 Gbit/s  NET_RX softirqs  cpu0   231675  cpu1   229011   packets processed  cpu0   281922  cpu1   285246
rps_cpus=3 (CPUs 0,1), run 1 31.2 Gbit/s  NET_RX softirqs  cpu0   185590  cpu1   202804   packets processed  cpu0   182494  cpu1   270663
rps_cpus=3 (CPUs 0,1), run 2 29.7 Gbit/s  NET_RX softirqs  cpu0   155230  cpu1   211673   packets processed  cpu0   161067  cpu1   238213
rps_cpus=3 (CPUs 0,1), run 3 31.4 Gbit/s  NET_RX softirqs  cpu0   160843  cpu1   223907   packets processed  cpu0   166054  cpu1   252040
```

**RPS made it slower**: **44.9, 46.4 and 48.9 Gbit/s without it, and 31.2, 29.7 and 31.4 with it**, a drop of about a third, outside the spread of the runs. The counters show what changed: without RPS the packets were processed almost evenly (**227,881 and 239,543** in the first run) because the sender's and receiver's own CPUs each did their share; with RPS the second CPU processed **270,663 against 182,494**, and every packet now had a **hand-off between CPUs**. My reading is that with only two CPUs, **both already busy running the four streams**, RPS only adds an inter-processor interrupt and a cold cache to each packet; I measured the throughput and the counters, not the cause. **RPS pays when one CPU is the receive bottleneck and others are idle**, which is the case for a single-queue NIC on a big machine, not this one. The general rule: **steering is a way to move work to idle CPUs, so first check there is idle CPU to move it to**.

### 6.3 Rings, offloads, coalescing and polling

```text
--- 6. the network card
$ ethtool -i eth0 | head -1
driver: virtio_net
$ ethtool -g eth0 | grep -A3 -E 'Pre-set|Current' | grep -E 'RX:|TX:'
RX:                     256
RX:                     256
$ ethtool -k eth0 | grep -E '^(generic-receive-offload|large-receive-offload|tcp-segmentation-offload|generic-segmentation-offload|rx-checksumming)'
rx-checksumming: on [fixed]
tcp-segmentation-offload: on
generic-segmentation-offload: on
generic-receive-offload: on
large-receive-offload: off [fixed]
$ ethtool -c eth0 2>&1 | head -2
Coalesce parameters for eth0:
Adaptive RX: off  TX: n/a
$ ethtool -l eth0 2>&1 | head -3
Channel parameters for eth0:
Pre-set maximums:
RX:             n/a
```

| Knob | This NIC | Latency vs throughput (from memory) |
| --- | --- | --- |
| **Ring size** (`ethtool -g`) | RX and TX **256** of a maximum 256 | larger rings absorb bursts (fewer drops, more throughput) but hold more packets in the queue (**more delay**); a small ring is the latency choice |
| **GRO** (generic receive offload) | **on** | merges consecutive segments of a flow into one large packet: **much less CPU per byte**, but a packet may wait to be merged; off for the lowest latency |
| **TSO and GSO** | **on** | the stack hands one huge segment down and the NIC (or the last stage) cuts it: less CPU for sending |
| **LRO** | off **[fixed]** | hardware merge; breaks forwarding, avoided on routers |
| **Interrupt coalescing** (`ethtool -c`, `-C rx-usecs`) | adaptive RX **off** | interrupt once per N packets or µs: **fewer interrupts, more delay**; `rx-usecs 0` for latency |
| **Busy polling** (`net.core.busy_poll`, `busy_read`) | **0** (off) | the thread polls the NIC's queue for up to N µs instead of sleeping: **lower latency, a burned CPU** |
| **NAPI budget** (`netdev_budget`) | **300** packets and **8,000 µs** | how much one softirq run may do before yielding; larger helps throughput and can starve other softirqs |

`[fixed]` means the driver does not allow the change. The extreme end of this is **kernel bypass** (DPDK, `AF_XDP`, XDP programs): a thread polls the NIC directly and the kernel's network stack is out of the path. It buys a per-packet cost of tens of nanoseconds and costs every service the kernel provides (firewall, routing, `tcpdump`, sockets), which is the reason it is used for packet processors and trading gateways and not for general servers.


---

## 7. Memory: huge pages, faults and NUMA

### 7.1 The TLB, and what a page size does to it

Every memory access needs a **virtual to physical translation**. The CPU caches recent translations in the **TLB**, a small table (a core's second-level TLB holds on the order of 1,500 to 2,000 entries on recent Intel parts, from memory). A miss costs a **page-table walk**: several dependent memory reads, and **in a virtual machine the walk is two-dimensional** (guest table, then host table), so a miss costs more still. The **reach** of the TLB is entries times page size. With 2,048 entries, **4 KB pages reach 8 MB, 2 MB pages reach 4 GB** (arithmetic; Problem C prints it). A program that walks a few hundred megabytes at random thrashes a 4 KB TLB on every access and does not thrash a 2 MB one.

### 7.2 Measured: a random pointer chase

The test is the harshest honest one: a buffer of one cache line per element, linked into a **random cycle**, followed for 20 million **dependent** loads (each address comes from the previous load, so nothing can be overlapped or prefetched). The whole program (`tlb.c`; `gcc -O2 -o tlb tlb.c`, run as `./tlb 512 thp`):

```c
/* tlb.c: random pointer chase over a buffer; ns per dependent load with 4 KB pages, THP, or hugetlbfs.
   usage: tlb MB mode   (mode: 4k | thp | huge) */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static long smaps_kb(const char *key) {
    FILE *f = fopen("/proc/self/smaps_rollup", "r"); char l[256]; long v = -1;
    while (f && fgets(l, sizeof l, f)) if (!strncmp(l, key, strlen(key))) { sscanf(l + strlen(key), " %ld", &v); break; }
    if (f) fclose(f);
    return v;
}

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    size_t mb = atol(argv[1]), bytes = mb << 20, n = bytes / 64;
    const char *mode = argv[2];
    int flags = MAP_PRIVATE | MAP_ANONYMOUS | (!strcmp(mode, "huge") ? MAP_HUGETLB : 0);
    char *buf = mmap(NULL, bytes, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (buf == MAP_FAILED) { perror("mmap"); return 1; }
    if (!strcmp(mode, "thp")) madvise(buf, bytes, MADV_HUGEPAGE);
    size_t *next = malloc(n * sizeof *next), *perm = malloc(n * sizeof *perm);
    for (size_t i = 0; i < n; i++) perm[i] = i;
    srand(1);
    for (size_t i = n - 1; i > 0; i--) { size_t j = (size_t)rand() % i; size_t t = perm[i]; perm[i] = perm[j]; perm[j] = t; }
    for (size_t i = 0; i < n; i++) next[perm[i]] = perm[(i + 1) % n];
    for (size_t i = 0; i < n; i++) *(size_t *)(buf + i * 64) = next[i];
    free(next); free(perm);
    size_t p = 0; long steps = 20000000;
    for (long i = 0; i < 2000000; i++) p = *(size_t *)(buf + p * 64);
    double t0 = now();
    for (long i = 0; i < steps; i++) p = *(size_t *)(buf + p * 64);
    double dt = now() - t0;
    printf("%4zu MB %-4s  %6.1f ns per load  AnonHugePages %ld kB%s\n", mb, mode, dt * 1e9 / steps,
           smaps_kb("AnonHugePages:"), p == (size_t)-1 ? "!" : "");
    return 0;
}
```

The same loop runs on three kinds of memory: ordinary 4 KB pages; the same memory with **`madvise(MADV_HUGEPAGE)`**, which asks for **transparent huge pages**; and **`MAP_HUGETLB`**, which uses pages **reserved in advance** by `vm.nr_hugepages`. The settings first:

```text
--- 0. the huge-page settings
$ cat /sys/kernel/mm/transparent_hugepage/enabled /sys/kernel/mm/transparent_hugepage/defrag
always [madvise] never
always defer defer+madvise [madvise] never
$ grep -E 'Hugepagesize|HugePages_Total' /proc/meminfo
HugePages_Total:       0
Hugepagesize:       2048 kB
$ grep -E 'HugePages_Total|HugePages_Free' /proc/meminfo
HugePages_Total:    1200
HugePages_Free:     1200
```

THP is in **`madvise`** mode (only memory that asks gets huge pages), and 1,200 pages of 2 MB (2.3 GB) were reserved for the third kind. The results, in nanoseconds per load, with `AnonHugePages` read from `/proc/self/smaps_rollup` to prove which pages the program really got:

```text
--- 1. 20 million dependent loads at random, ns per load
   4 MB 4k      57.3 ns per load  AnonHugePages 0 kB
   4 MB thp     55.0 ns per load  AnonHugePages 4096 kB
   4 MB huge    53.6 ns per load  AnonHugePages 0 kB
  64 MB 4k     172.5 ns per load  AnonHugePages 0 kB
  64 MB thp    161.4 ns per load  AnonHugePages 65536 kB
  64 MB huge   159.0 ns per load  AnonHugePages 0 kB
 512 MB 4k     201.5 ns per load  AnonHugePages 0 kB
 512 MB thp    165.0 ns per load  AnonHugePages 524288 kB
 512 MB huge   171.8 ns per load  AnonHugePages 0 kB
 512 MB 4k     213.0 ns per load  AnonHugePages 0 kB
 512 MB thp    157.7 ns per load  AnonHugePages 524288 kB
 512 MB huge   170.2 ns per load  AnonHugePages 0 kB
 512 MB 4k     199.0 ns per load  AnonHugePages 0 kB
 512 MB thp    160.8 ns per load  AnonHugePages 524288 kB
 512 MB huge   164.7 ns per load  AnonHugePages 0 kB
2048 MB 4k     254.2 ns per load  AnonHugePages 0 kB
2048 MB thp    170.2 ns per load  AnonHugePages 2097152 kB
2048 MB huge   168.9 ns per load  AnonHugePages 0 kB
2048 MB 4k     264.4 ns per load  AnonHugePages 0 kB
2048 MB thp    174.3 ns per load  AnonHugePages 2097152 kB
2048 MB huge   167.2 ns per load  AnonHugePages 0 kB
2048 MB 4k     266.6 ns per load  AnonHugePages 0 kB
2048 MB thp    173.3 ns per load  AnonHugePages 2097152 kB
2048 MB huge   172.9 ns per load  AnonHugePages 0 kB
```

| Buffer | 4 KB pages | THP (`madvise`) | `MAP_HUGETLB` | Runs | Reading |
| --- | --- | --- | --- | --- | --- |
| 4 MB | 57.3 | 55.0 | 53.6 | 1 | no difference: it fits in the 4 MB L2 cache |
| 64 MB | 172.5 | 161.4 | 159.0 | 1 | 6 to 8 percent, **one run each, only suggestive** |
| 512 MB | 199.0 to 213.0 | 157.7 to 165.0 | 164.7 to 171.8 | 3 | about **21 percent** less time |
| 2,048 MB | 254.2 to 266.6 | 170.2 to 174.3 | 167.2 to 172.9 | 3 | about **34 percent** less time |

Three conclusions. **The gain grows with the working set**: nothing at 4 MB, a third of the time at 2 GB. **THP and reserved huge pages are the same speed**; the difference between them is how you get them (below). And in this VM, which has a large L3 (260 MB), even the "all in cache" sizes pay a hundred nanoseconds or more per load because the translation cost is on top. On a bare-metal machine the same program usually shows a **larger** relative gain because there the TLB miss and the DRAM access are the only costs (from memory); I did not measure that.

The kernel's own counter agrees with the program's. One 512 MB THP run **raised `thp_fault_alloc` from 5,220 to 5,476: exactly 256 huge pages**, 512 MB divided by 2 MB:

```text
--- 2. how many huge pages did the kernel build
thp_fault_alloc went from 5220 to 5476 for one 512 MB run (512 MB / 2 MB = 256)
```

### 7.3 Two ways to get huge pages, and what each costs

| | **THP** (transparent) | **hugetlb** (reserved) |
| --- | --- | --- |
| How | the kernel gives a 2 MB page to an aligned 2 MB region on fault, or **`khugepaged`** merges small pages later | `vm.nr_hugepages`, `hugepages=` on the boot line, then `mmap(MAP_HUGETLB)` or a `hugetlbfs` mount |
| Control | `enabled` is **`always`, `madvise` or `never`**; `defrag` says how hard the kernel works to find a free 2 MB block | exact: N pages, taken out of general memory for good |
| Risk | **compaction stalls**: to build a 2 MB page the kernel may move pages around in the **fault path**; memory **bloat** (a sparse region becomes 2 MB) | the reservation **fails if memory is fragmented** (below); the pages are **not swappable** and not general memory |
| Used by | most programs by default; latency-critical databases often set it to `never` or `madvise` | DPDK, databases with large shared buffers, JVMs with `-XX:+UseLargePages`, VMs |

For latency work the usual setting is **`madvise` (the default here) or `never`**, plus `hugetlb` for the few buffers that need it. `lcheck.sh` in section 9 flags `always`. Reserve at **boot** when you can, because a running system's memory is fragmented. Asking for far too many shows the limit:

```text
--- 3. asking for more reserved pages than can be had
$ grep -E 'HugePages_Total' /proc/meminfo
HugePages_Total:    3177
```

The request was for 200,000 pages (400 GB) on an 8 GB machine; the kernel **reserved 3,177 (6.2 GB) and stopped**, silently: **always read back `HugePages_Total`**. The CPU also lists `pdpe1gb`, so 1 GB pages exist (`default_hugepagesz=1G hugepagesz=1G hugepages=N` at boot; they cannot be allocated reliably later). Containers count reserved pages through the **`hugetlb` cgroup controller**, which this kernel has (`CONFIG_CGROUP_HUGETLB=y`).

### 7.4 Page faults, reclaim and locking

A **page fault** on the hot path costs a microsecond or more when minor (from memory) and **milliseconds** when it needs the disk or when the kernel must **reclaim** memory first. Remove the cause rather than the symptom:

- **Pre-fault and lock**: `mlockall(MCL_CURRENT | MCL_FUTURE)` pins all pages in RAM; `MAP_POPULATE` or touching each page at start-up makes them present before the loop begins; both need `RLIMIT_MEMLOCK` raised.
- **Keep swap out of the path**: `vm.swappiness` (60 here) lower, or no swap on the latency host; a locked page is never swapped.
- **Keep free memory available**: `vm.min_free_kbytes` (67,584 here) sets the watermark below which allocating tasks do **direct reclaim** themselves; a larger value makes the background `kswapd` start sooner (it costs memory that is never used).
- **Keep writeback out of the way**: `vm.dirty_ratio` 20 and `vm.dirty_background_ratio` 10 here; a process that dirties pages past the first limit is made to **write them out itself**, which shows up as a stall (LX 07).
- **Do not let the kernel move your pages**: `kernel.numa_balancing` migrates pages toward the CPUs that use them, with page faults as the cost; it is `0` here, and `0` on a latency box.

### 7.5 NUMA

On a multi-socket machine each socket has its own memory controller; a CPU reads **local** memory fast and **remote** memory through the interconnect, typically **one and a half to two times slower** and with less bandwidth (from memory; the kernel's own distance table says `10` for local and a larger number, often `20` to `32`, for remote). This VM has one node, so there is nothing to place:

```text
--- 4. NUMA on this VM
$ numactl -H
available: 1 nodes (0)
node 0 cpus: 0 1
node 0 size: 8024 MB
node 0 free: 6838 MB
node distances:
node   0
  0:  10
$ cat /sys/devices/system/node/node0/distance
10
$ numactl --cpunodebind=0 --membind=0 true && echo bound run worked
bound run worked
```

The commands are still the right ones: `numactl -H` for the topology, `numastat -p PID` for where a process's pages are, `numactl --cpunodebind=N --membind=N CMD` to run on one node's CPUs and memory (it worked here on node 0, the only node). The rules, all from memory:

1. **Memory goes where it is first touched.** Allocate and initialise a buffer **on the thread that will use it**, or the first thread (often on another node) owns it.
2. **A PCIe device belongs to a node** (`/sys/class/net/eth0/device/numa_node`); run the thread that polls it, and allocate its buffers, **on that node**. Crossing the interconnect for every packet halves what the NIC can do.
3. **Interleave** (`numactl --interleave=all`) for a large structure shared by threads on all nodes, when bandwidth matters more than latency.
4. **Do not use `numa_balancing` for latency work**; place things yourself.
5. **A guest does not see the host's topology** unless the hypervisor exposes it: in a VM, a "local" access may be remote.

---

## 8. Sysctls and throughput

### 8.1 The bandwidth-delay product

A TCP connection can have at most **one window** of unacknowledged data in flight, so its throughput is at most **window / round-trip time**. To fill a link the window must equal the **bandwidth-delay product**: bandwidth times RTT.

```text
--- 2. bandwidth-delay product (arithmetic, not measured)
link         RTT        bytes in flight to fill it
1 Gbit/s     1 ms       125 KB
10 Gbit/s    1 ms       1.25 MB
10 Gbit/s    50 ms      62.50 MB
100 Gbit/s   0.1 ms     1.25 MB
0.1 Gbit/s   40 ms      500 KB
a 128 KB window at 20 ms RTT caps one connection at 52.4 Mbit/s
```

A 10 Gbit/s link with a 50 ms round trip needs **62.5 MB in flight**; a 128 KB window on a 20 ms path caps one connection at **52.4 Mbit/s** whatever the link can carry. (Problem F does the same sum for 30 ms.) This is why long, fat paths (between data centres) need big buffers and short paths do not.

The kernel **autotunes** the receive buffer, growing it as the connection proves it can use it, up to the third number of `net.ipv4.tcp_rmem`. A live loopback connection shows it:

```text
--- 3. receive-buffer autotuning on a live loopback connection
$ ss -tmn state established '( sport = :5201 )' | grep -o 'skmem:([^)]*)'
skmem:(r0,rb131072,t0,tb3939840,f0,w0,o0,bl0,d0)
skmem:(r1385959,rb1864135,t0,tb3939840,f2585,w0,o0,bl0,d21)
```

The first line is the **listening socket**, still at the default **`rb131072`** (128 KB). The second is the **data connection**, whose receive buffer **grew to `rb1864135`** (1.8 MB) with **1,385,959 bytes** of receive memory in use and a send buffer **`tb3939840`** (3.9 MB). Two practical points. **An application that calls `setsockopt(SO_RCVBUF)` turns autotuning off for that socket** and is then limited by `net.core.rmem_max` (4 MB here), so on a long path the best setting is often to **set nothing** and raise `tcp_rmem`'s maximum. And a buffer that is **too** large is not free: more memory per connection, and a queue that adds delay when the path is congested (**bufferbloat**).

### 8.2 The values on this machine, and which way each pulls

```text
--- 1. the sysctls that matter for latency and throughput, with this VM's values
net.core.rmem_max = 4194304
net.core.wmem_max = 4194304
net.ipv4.tcp_rmem = 4096        131072  33554432
net.ipv4.tcp_wmem = 4096        16384   4194304
net.core.somaxconn = 4096
net.core.netdev_max_backlog = 1000
net.core.netdev_budget = 300
net.core.netdev_budget_usecs = 8000
net.core.busy_poll = 0
net.core.busy_read = 0
net.ipv4.tcp_congestion_control = bbr
net.core.default_qdisc = pfifo_fast
net.ipv4.tcp_slow_start_after_idle = 1
net.ipv4.tcp_notsent_lowat = 4294967295
vm.swappiness = 60
vm.dirty_ratio = 20
vm.dirty_background_ratio = 10
vm.min_free_kbytes = 67584
vm.zone_reclaim_mode = 0
kernel.numa_balancing = 0
kernel.sched_autogroup_enabled = 1
kernel.timer_migration = 1
kernel.nmi_watchdog = 0
kernel.watchdog = 1
```

| Sysctl | Here | Latency or throughput | Direction (from memory) |
| --- | --- | --- | --- |
| `net.ipv4.tcp_rmem`, `tcp_wmem` (min, default, max) | 4 KB, 128 KB, **32 MB** and 4 KB, 16 KB, **4 MB** | throughput on long paths | raise the **max** for high BDP; autotuning does the rest |
| `net.core.rmem_max`, `wmem_max` | 4 MB | caps `SO_RCVBUF` and `SO_SNDBUF` | raise only if applications set them |
| `net.core.somaxconn` | 4,096 | accept-queue size cap | raise for a server that takes bursts of new connections |
| `net.core.netdev_max_backlog` | 1,000 | per-CPU queue before softirq; overflow is a drop (second column of `softnet_stat`) | raise for bursty 10 Gbit/s and faster |
| `net.core.netdev_budget`, `_usecs` | 300, 8,000 | one softirq run's budget | raise for throughput, lower for latency |
| `net.core.busy_poll`, `busy_read` | 0, 0 | latency | 50 (µs) polls instead of sleeping, burning CPU |
| `net.ipv4.tcp_congestion_control` | **bbr** | throughput on lossy or long paths | `bbr` or `cubic`; BBR is delay-based and pairs with the `fq` qdisc |
| `net.core.default_qdisc` | `pfifo_fast` | queueing delay | `fq` or `fq_codel` against bufferbloat |
| `net.ipv4.tcp_slow_start_after_idle` | **1** | bursty long-lived connections | **0** keeps the window after a pause instead of restarting small |
| `net.ipv4.tcp_notsent_lowat` | 4294967295 (no limit) | latency | a small value (for example 16 KB) stops an app queueing seconds of data in the kernel |
| `vm.swappiness` | 60 | latency | 1 to 10, or no swap |
| `vm.dirty_ratio`, `_background_ratio` | 20, 10 | write stalls | lower for steadier latency, higher for batch throughput |
| `vm.min_free_kbytes` | 67,584 | direct-reclaim stalls | raise on a big machine with bursty allocation |
| `kernel.numa_balancing` | 0 | jitter | 0 |
| `kernel.sched_autogroup_enabled` | 1 | fairness between sessions | 0 on a server |
| `kernel.watchdog`, `nmi_watchdog` | 1, 0 | a periodic timer or NMI per CPU | off on isolated CPUs (`nowatchdog`, `watchdog_cpumask`) |

### 8.3 Throughput tuning is a diagnosis first

The USE method of LX 05 applies unchanged. **Find what saturates before touching a knob**: `mpstat -P ALL 1` shows one CPU at 100 percent in softirq (spread the work: RSS, RPS, more queues, section 6), `ss -ti` shows a connection limited by its window or by `cwnd` (buffers, congestion control), `ethtool -S` shows drops at the NIC (ring size, `netdev_max_backlog`), a link at line rate means you are **done**. Then the standard levers, in the order they usually pay: **batching** (GRO, TSO, larger reads and writes, `writev`, `sendmmsg`), **more parallelism** (queues, connections, threads, with the flows kept on one CPU), **bigger buffers**, **bigger frames** (a jumbo MTU of 9,000 if the whole path agrees), **less copying** (`sendfile`, `splice`, zero-copy), and last **kernel bypass**.

### 8.4 `tuned` profiles (from memory)

Many distributions ship **`tuned`**, a daemon that applies a named bundle of settings: **`throughput-performance`**, **`latency-performance`**, **`network-latency`**, **`network-throughput`**, **`realtime`**, **`cpu-partitioning`** (isolated cores, with the boot line written for you), `virtual-guest`. `tuned-adm profile NAME` applies one, `tuned-adm active` names it, `tuned-adm verify` checks the system still matches. A profile is **a list of the settings in this tutorial** (governor, `force_latency`, `transparent_hugepages`, `numa_balancing`, `busy_poll`, `swappiness`, `isolated_cores`) written down once. **Read the profile under `/usr/lib/tuned/` before applying it**, and measure after: it is the same change you would make by hand, with the same price. It is not installed here.

---

## 9. An audit script

Before tuning, write down what the host is set to. `lcheck.sh` (read-only) prints one line per setting as **OK** (fine for latency work), **WARN** (a known hazard) or **NOTE** (a fact to compare with your goal), and it works on a machine with no tools beyond `bash`, `sysctl`, `zcat` and `python3`:

```bash
#!/usr/bin/env bash
# lcheck.sh: read-only audit of the latency-tuning state of this host. One line per check: OK, NOTE or WARN.
say() { printf '%-5s %-22s %s\n' "$1" "$2" "$3"; }
rd() { cat "$1" 2>/dev/null; }
cfg() { zcat /proc/config.gz 2>/dev/null | grep -E "^$1=" | head -1 | cut -d= -f2; }

iso=$(rd /sys/devices/system/cpu/isolated)
[ -n "$iso" ] && say OK isolated_cpus "$iso" || say NOTE isolated_cpus "none (no isolcpus= on the command line)"
nf=$(rd /sys/devices/system/cpu/nohz_full)
if [ -n "$nf" ]; then say OK nohz_full "$nf"
elif [ "$(cfg CONFIG_NO_HZ_FULL)" = y ]; then say NOTE nohz_full "built in, not enabled"
else say NOTE nohz_full "kernel built without CONFIG_NO_HZ_FULL"; fi
say NOTE tick_rate "CONFIG_HZ=$(cfg CONFIG_HZ)"
for p in $(grep -oE '(isolcpus|nohz_full|rcu_nocbs|irqaffinity|idle|processor.max_cstate|intel_idle.max_cstate|intel_pstate|transparent_hugepage|hugepages|nosoftlockup)=[^ ]*' /proc/cmdline); do say NOTE cmdline "$p"; done
if [ -r /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]; then
  g=$(rd /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)
  [ "$g" = performance ] && say OK cpu_governor "$g" || say WARN cpu_governor "$g (frequency changes add latency)"
else say NOTE cpu_governor "no cpufreq interface (virtual machine or firmware controlled)"; fi
say NOTE cpuidle "driver $(rd /sys/devices/system/cpu/cpuidle/current_driver), governor $(rd /sys/devices/system/cpu/cpuidle/current_governor)"
thp=$(sed -E 's/.*\[(.*)\].*/\1/' /sys/kernel/mm/transparent_hugepage/enabled)
[ "$thp" = always ] && say WARN thp_enabled "always (compaction can stall a latency path)" || say OK thp_enabled "$thp"
say NOTE hugetlb_pages "$(sysctl -n vm.nr_hugepages) reserved"
nodes=$(ls -d /sys/devices/system/node/node[0-9]* | wc -l)
say NOTE numa_nodes "$nodes"
[ "$(sysctl -n kernel.numa_balancing)" = 1 ] && [ "$nodes" -gt 1 ] && say WARN numa_balancing "on (page migration jitter)" || say OK numa_balancing "$(sysctl -n kernel.numa_balancing) with $nodes node(s)"
rt=$(sysctl -n kernel.sched_rt_runtime_us)
[ "$rt" = -1 ] && say WARN rt_throttle "off (a runaway SCHED_FIFO task can lock a CPU)" || say OK rt_throttle "$rt of $(sysctl -n kernel.sched_rt_period_us) us"
if [ "$(cfg CONFIG_PREEMPT_RT)" = y ]; then pm=PREEMPT_RT
elif [ "$(cfg CONFIG_PREEMPT)" = y ]; then pm=PREEMPT
elif [ "$(cfg CONFIG_PREEMPT_VOLUNTARY)" = y ]; then pm=VOLUNTARY
else pm="NONE (dynamic: $(cfg CONFIG_PREEMPT_DYNAMIC))"; fi
say NOTE preempt_model "$pm"
pgrep -x irqbalance >/dev/null && say NOTE irqbalance "running (moves IRQs onto any CPU unless banned)" || say NOTE irqbalance "not running"
bad=0
if [ -n "$iso" ]; then
  for f in /proc/irq/[0-9]*/smp_affinity_list; do
    a=$(rd "$f"); python3 - "$iso" "$a" <<'PY' || bad=$((bad+1))
import sys
def s(x):
    r=set()
    for p in x.split(','):
        if '-' in p: a,b=p.split('-'); r|=set(range(int(a),int(b)+1))
        elif p: r.add(int(p))
    return r
sys.exit(1 if s(sys.argv[1]) & s(sys.argv[2]) else 0)
PY
  done
  [ $bad -eq 0 ] && say OK irq_vs_isolated "no IRQ is allowed on an isolated CPU" || say WARN irq_vs_isolated "$bad IRQs may fire on isolated CPUs"
else say NOTE irq_vs_isolated "nothing isolated, nothing to check"; fi
say NOTE timer_slack "$(rd /proc/self/timerslack_ns) ns default for normal tasks"
say NOTE swappiness "$(sysctl -n vm.swappiness)"
say NOTE watchdog "nmi_watchdog=$(sysctl -n kernel.nmi_watchdog) (a periodic NMI per CPU)"
```

On this VM:

```text
$ ./lcheck.sh
NOTE  isolated_cpus          none (no isolcpus= on the command line)
NOTE  nohz_full              kernel built without CONFIG_NO_HZ_FULL
NOTE  tick_rate              CONFIG_HZ=250
NOTE  cpu_governor           no cpufreq interface (virtual machine or firmware controlled)
NOTE  cpuidle                driver none, governor menu
OK    thp_enabled            madvise
NOTE  hugetlb_pages          0 reserved
NOTE  numa_nodes             1
OK    numa_balancing         0 with 1 node(s)
OK    rt_throttle            950000 of 1000000 us
NOTE  preempt_model          NONE (dynamic: y)
NOTE  irqbalance             not running
NOTE  irq_vs_isolated        nothing isolated, nothing to check
NOTE  timer_slack            50000 ns default for normal tasks
NOTE  swappiness             60
NOTE  watchdog               nmi_watchdog=0 (a periodic NMI per CPU)
```

Read each line against the earlier sections. **There is no isolation** (no `isolcpus`, no `nohz_full`, which the kernel was not even built for); the **tick is 250 Hz**; **no `cpufreq` interface** and **no `cpuidle` driver** (the host's business); **THP is `madvise`** (OK) and **no huge pages are reserved**; one NUMA node and balancing off; the **throttle is on (950,000 of 1,000,000 µs)**; the kernel is **`none`-preemptive**; `irqbalance` is not running; the **timer slack is 50,000 ns**. On a real latency host, the same script should show `isolated_cpus` and `nohz_full` equal, an `OK` for `irq_vs_isolated` (**no IRQ may fire on an isolated CPU**, the check that catches the most mistakes), a `performance` governor, `PREEMPT` or `PREEMPT_RT`, and reserved huge pages. **A script that checks is better than a document that says**: keep it in the repository with the machine's configuration and run it after every reboot and kernel update.

---

## 10. A worked recipe: one thread that must not be late

**The task.** A thread must react to an event within 100 µs, 99.9 percent of the time, on a busy machine. Here is the order of work, with what each step did in this VM where it could be tried.

### 10.1 The steps, and the cumulative measurement

1. **Measure the baseline under load** (section 1.3): three roaming burners, the thread pinned with `taskset` to CPU 1.
2. **Fence it** (section 3.3): a `cpuset` owns CPU 1; everything else is confined to CPU 0.
3. **Keep the CPU awake** (section 4): a `SCHED_IDLE` spinner inside the fence. (On bare metal: C-state limits.)
4. **Remove timer slack** (section 2.3) or run the thread as `SCHED_FIFO` with a modest priority (section 5), **and keep CPU 0 free** for the administrator.
5. **Steer IRQs away** (section 6): not possible here; on a real host `irqaffinity=` and `irq_vs_isolated` OK.
6. **Fix memory** (section 7): `mlockall`, pre-fault, huge pages for the big buffers, NUMA-local.
7. **Verify** with `wake`/`jitter`, then a **long soak** at the real load.

Steps 1 to 3 measured together:

```text
--- 4. the same, plus a SCHED_IDLE spinner inside lat so CPU 1 never halts
lat cpuset + spinner, run 1  min    35  p50    69  p99    171  p99.9   1127  max    1786  over100us 69
lat cpuset + spinner, run 2  min    48  p50    69  p99    233  p99.9   1893  max    1896  over100us 45
lat cpuset + spinner, run 3  min    65  p50    69  p99    376  p99.9  11753  max   13693  over100us 44
$ ls /sys/fs/cgroup/cpuset
cgroup.clone_children
cgroup.procs
cgroup.sane_behavior
cpuset.cpu_exclusive
cpuset.cpus
cpuset.effective_cpus
cpuset.effective_mems
cpuset.mem_exclusive
cpuset.mem_hardwall
cpuset.memory_migrate
cpuset.memory_pressure
cpuset.memory_pressure_enabled
cpuset.memory_spread_page
cpuset.memory_spread_slab
cpuset.mems
cpuset.sched_load_balance
cpuset.sched_relax_domain_level
notify_on_release
release_agent
tasks
```

| Setup | p50 (µs) | p99 (µs) | Wake-ups over 100 µs (of 3,000) |
| --- | --- | --- | --- |
| 1. `taskset` pin, burners roam | 68 to 69 | **3,037 to 9,483** | 131 to 180 |
| 2. `cpuset` fence, CPU 1 otherwise idle | 117 to 125 | **216 to 239** | 2,024 to 2,238 |
| 3. fence **plus** keep-awake spinner | **69** | **171 to 376** | **44 to 69** |

Each step fixed one thing and the third kept both gains: **the median of the pinned task with the tail of the fenced one**. The p99.9 and maximum of the third (**1,127 to 11,753 µs** and up to 13.7 ms) are still the host's, and the 100 µs goal is **not** met at p99.9 on this VM by any setting. That is the honest result: **the tuning removed what the guest could remove**, and what remains needs a dedicated host. The experiment is the method; the numbers belong to this VM.

### 10.2 The boot line for a real isolated-core server (from memory)

A 16-core, two-socket machine whose NIC is on node 0, with cores 0 and 1 kept for the system and cores 2 to 7 (node 0) given to the latency threads. This is the **shape** of the line, to adapt and test, not a recipe to paste:

```text
isolcpus=domain,managed_irq,2-7 nohz_full=2-7 rcu_nocbs=2-7 irqaffinity=0-1
processor.max_cstate=1 intel_idle.max_cstate=1 nosoftlockup tsc=reliable
default_hugepagesz=1G hugepagesz=1G hugepages=8 transparent_hugepage=madvise
```

Then at run time: the NIC's IRQs to cores 0 and 1 (`smp_affinity_list`), the latency threads to cores 2 to 7 with `SCHED_FIFO` priority 80 and `mlockall`, buffers from the 1 GB pages on **node 0**, the system's services restricted to cores 0, 1 and 8 to 15 (`AllowedCPUs=` in a slice), and `lcheck.sh` plus `jitter` on each isolated core showing nothing above a few microseconds. A tick-free core with a single `SCHED_FIFO` thread and no interrupts is the nearest thing to bare metal that Linux gives.


---

## 11. What it costs, and what breaks

| Change | Measured here | What it costs |
| --- | --- | --- |
| Pin with `taskset` | migrations **11 to 1** in 3 s; context switches 301 against 269 | **nothing is protected from sharing**: p99 stayed **3,037 to 9,483 µs** next to roaming burners |
| `cpuset` fence | p99 **216 to 239 µs** | a CPU **removed from everyone else**; someone must keep the rest of the system outside |
| Keep the CPU awake (`SCHED_IDLE` spinner) | wake-ups over 100 µs **1,371 to 1,780 down to 33 to 46** | one CPU **100 percent busy** doing nothing: power, heat, turbo headroom |
| `/dev/cpu_dma_latency` | **no effect** on a VM (driver `none`) | accepted and useless: only a measurement shows it |
| Timer slack 50 µs to 1 ns | median **104 to 107 down to 53 to 68 µs** | no timer coalescing: more wake-ups, more power |
| `SCHED_FIFO` | median **14 to 17 µs** | a runaway thread: **5.3 percent** left for others, lower FIFO tasks **starved 3.5 s**, even a **higher** FIFO task waited **45 ms** |
| RPS on a two-CPU loopback | **47 down to 31 Gbit/s** | an IPI and a cold cache per packet; useful only with idle CPUs to steer to |
| Huge pages | **254 to 267 down to 167 to 174 ns** per load at 2 GB; nothing at 4 MB | THP: compaction stalls and bloat; hugetlb: **memory taken for good**, a request for 200,000 pages silently gave **3,177** |
| Big socket buffers | receive buffer autotuned to **1.8 MB** on loopback | memory per connection; queueing delay if the path is congested |
| `nice -n -20` | median **unchanged** (67 µs); p99 helped in 4 of 5 runs | little at wake-up; it matters for sharing |
| Boot isolation (`isolcpus`, `nohz_full`, `rcu_nocbs`) | not run | housekeeping CPUs carry the load; the boot line is **per machine** |

**What breaks.** A **latency tuning applied to a shared host** takes the cores other services needed: watch their latency too. A **FIFO thread without a block** hangs a CPU for 50 ms in every second and, with the throttle off, **for ever**. A **pinned thread on a CPU that gets IRQs** is slow in a way no profiler of the thread will show; read `/proc/interrupts`. A **setting accepted and ignored** (the QoS file here, a governor in a guest) looks like success. **THP `always` plus a long-running database** causes stalls that look like random network trouble. A tuning **lost on reboot** (a `sysctl -w`, an `echo` into `/sys`) quietly returns the old behaviour: persist it in `/etc/sysctl.d/`, the boot line or a unit, and check with `lcheck.sh`.

---

## 12. Common mistakes

1. **Tuning without a baseline, or with one run.** The identical idle runs gave p99.9 of **579, 1,037 and 1,367 µs** (section 2.2); a "10 percent gain" in one run is noise. Compare **ranges over several runs**, and the same load.
2. **Measuring the average.** A mean of 70 µs can hold a 3 ms stall every second. Use p99, p99.9, the maximum and the **count over your threshold**.
3. **Measuring an idle machine** and tuning for a loaded one, or the reverse: the section 4 effect **reverses** between them (the idle CPU had the worse median and the better tail).
4. **Pinning a thread and believing it is isolated.** Pinning chooses where the thread runs, not who else runs there. Fence the CPU, move interrupts, stop the tick.
5. **`SCHED_FIFO` everywhere.** Real-time priority is for one or two threads that block every iteration. Several spinning FIFO threads starve the system (section 5.3).
6. **Setting the real-time throttle to `-1`** "to remove the 5 percent" with no second way into the machine.
7. **Pinning the latency thread to CPU 0**, where the interrupts, the kernel threads and your shell live.
8. **Applying a `tuned` profile or a blog's `sysctl` list without reading it**, and without measuring. Half of those settings do nothing on your hardware, a few hurt.
9. **THP `always`, or `drop_caches` and `compact_memory` in a cron job**, as latency tuning. Compaction **is** the stall.
10. **Setting `SO_RCVBUF`** and so turning off autotuning, then capping a long-path connection at `rmem_max`.
11. **Forgetting the host**: tuning a cloud VM's guest for a target the host's scheduler cannot meet. Measure the tail on the **instance type** you will run on, and ask for a dedicated or bare-metal one if the tail is the host's.
12. **A tuning that does not survive a reboot**, and a boot line nobody wrote down.
13. **Trusting an accepted write.** The file accepted `0`, the CPU did not change (section 4.1). Read back and measure.

---

## 13. Questions out loud

1. **What is the difference between tuning for latency and for throughput?** Latency tuning removes sources of delay from one path and cares about the tail; throughput tuning amortises per-unit cost (batching, bigger buffers, offloads, parallel queues) and cares about the average and the CPU left over. They pull apart: batching raises throughput and delays each item; polling lowers latency and burns a core.
2. **Why report p99.9 and not the mean?** A latency is a distribution; the mean hides rare long delays. Report the median, p99, p99.9, the maximum and the count over the threshold, from several runs (the same idle test gave p99.9 of 579 to 1,367 µs).
3. **A thread is pinned to a CPU and still has jitter. Why?** Other tasks may share the CPU (a fence is needed), interrupts and softirqs land on it, the scheduler tick fires, the CPU wakes from idle, the frequency changes, page faults and TLB misses happen, and in a VM the host takes time. Each has its own remedy (sections 3 to 7).
4. **`taskset`, `cpuset`, `isolcpus`: how do they differ?** `taskset` is a request a program makes for itself; a `cpuset` is a fence the administrator builds, enforced by the kernel for a group; `isolcpus` (boot) removes the CPUs from the scheduler's balancing so nothing lands there unless pinned. None of them moves interrupts or stops the tick; `nohz_full`, `rcu_nocbs` and `irqaffinity` do.
5. **What is idle-exit latency and how do you remove it?** The time a halted CPU needs to start running again (a C-state exit, or a VM exit in a guest). Measured: median 97 to 106 µs on an idle CPU, 66 to 69 µs with a spinner keeping it awake. Remove it with C-state limits (`max_cstate`, `idle=poll`, `/dev/cpu_dma_latency`) or a busy loop, at the price of power.
6. **What does `SCHED_FIFO` do and what can go wrong?** The highest-priority runnable FIFO task runs until it blocks; equals run in arrival order. A task that never blocks starves lower tasks and, without the throttle, locks the CPU. The throttle gives 5 percent of each second to non-real-time tasks, blocks all real-time tasks together, and starved a lower FIFO task for 3.5 s while a higher one waited up to 45 ms.
7. **What is priority inversion?** A high-priority task waits for a lock held by a low-priority task that a medium-priority task keeps from running. The cure is **priority inheritance**: the holder runs at the waiter's priority (`PTHREAD_PRIO_INHERIT`; `PREEMPT_RT` does it for kernel locks).
8. **What does `PREEMPT_RT` change?** Spinlocks become sleeping locks with priority inheritance, interrupt handlers run as threads, and nearly everything is preemptible, so the worst-case latency is bounded; throughput drops somewhat. Mainline since 6.12.
9. **What does `nohz_full` do, and what does it need?** It stops the periodic tick on a CPU that runs exactly one task, so the task is not interrupted every 4 ms (at HZ=250). It needs `CONFIG_NO_HZ_FULL`, usually `isolcpus` and `rcu_nocbs` on the same CPUs, and one runnable task there; the housekeeping CPUs take the timekeeping.
10. **How do you keep interrupts off a CPU?** Write `smp_affinity_list` for each IRQ or boot with `irqaffinity=`; stop or configure `irqbalance`; for managed IRQs use `isolcpus=managed_irq`; check with `/proc/interrupts` that the counts on that CPU are not growing.
11. **RSS, RPS, RFS, XPS?** RSS is hardware hashing into several queues with an IRQ each; RPS does the same in software with an IPI; RFS steers a flow to the CPU where its consumer last ran; XPS picks the transmit queue by sending CPU. RPS only helps when there are idle CPUs to steer to: on a two-CPU loopback it cut 47 to 31 Gbit/s.
12. **When do huge pages help, and what are the risks?** When the working set is far beyond the TLB's reach (8 MB with 2,048 entries of 4 KB). Measured: nothing at 4 MB, about 21 percent less time at 512 MB, about 34 percent at 2 GB. Risks: THP compaction stalls and bloat; hugetlb reservations that fail or take memory for good.
13. **What are the NUMA rules?** First touch decides where memory lives; put the thread, its memory and its NIC on the same node; interleave for shared bandwidth; turn off `numa_balancing` for latency work; remember a guest may not see the host's topology.
14. **How large must a socket buffer be?** At least the bandwidth-delay product: 10 Gbit/s at 30 ms is 37.5 MB, and a 128 KB window caps one connection at 35 Mbit/s. Let autotuning work and raise the maximum in `tcp_rmem` and `tcp_wmem`; setting `SO_RCVBUF` turns autotuning off.
15. **How do you prove a tuning helped?** A written goal as a percentile, a baseline under realistic load, one change at a time, several runs of each, a gap bigger than the spread, the price stated, and a long soak at the end.
16. **A cloud VM misses a 100 µs target by far. Now what?** Measure on the instance type; find out whether the tail is the guest's (affinity, fence, RT, IRQs) or the host's (steal time, a descheduled vCPU: nothing inside fixes it); if it is the host, ask for dedicated or bare-metal capacity.

---

## 14. Problems

Solve each at a terminal in the Linux VM from `99_toolchain/README.md` (the programs `wake.c`, `jitter.c` and `tlb.c` are printed in sections 1.4 and 7.2 and the commands are in the transcripts; on a Mac there is no `taskset` or `/proc`). **Problems that make a CPU spin must be run from a shell pinned to another CPU, and killed from it** (`taskset -c 0 bash`).

**Problem A. Did it help?** A service thread is pinned to CPU 1 with a normal burner on the same CPU. Compare **normal priority, `nice -n -20` and `SCHED_FIFO 10`**, five runs of 1,000 samples each. *Where:* a terminal, `wake -n 1000 -a 1 [-f 10]`. *Approach:* collect the median and p99 of each run, compare the **ranges** of the medians first, then the **median of the five p99 values**; say which settings are clearly different and which runs are outliers.

**Problem B. A fence.** Build a version 1 `cpuset` that owns CPU 1 and memory node 0, put a shell in it, and prove from `/proc` that it can run only on CPU 1. *Where:* `/sys/fs/cgroup/cpuset`. *Approach:* make a directory, write `cpuset.cpus` and `cpuset.mems`, write the shell's pid into `tasks`, read `Cpus_allowed_list` from `/proc/self/status`.

**Problem C. TLB reach.** A TLB holds 2,048 entries. How much memory does it reach with 4 KB, 2 MB and 1 GB pages? For buffers of 4, 64, 512 and 2,048 MB, how many pages is that? Predict where huge pages start to matter, then check against section 7.2. *Where:* paper, then `tlb 512 4k` against `tlb 512 thp` with `nr_hugepages` for the third mode. *Approach:* reach is entries times page size; the effect starts when the working set exceeds the reach **and** the data misses the caches.

**Problem D. A runaway.** A `SCHED_FIFO 99` loop is started on CPU 1 by mistake. Using section 5.3, say what an ordinary task, a `SCHED_FIFO 40` task and a `SCHED_FIFO 99` task on the same CPU can expect, and what you would have configured so that the mistake cannot hang the machine. *Where:* paper, from the measured transcript (do **not** reproduce it without a way to kill the spinner from another CPU). *Approach:* the throttle's 950,000 of 1,000,000 µs, who gets the gap, and `RLIMIT_RTTIME`.

**Problem E. Where did 40 µs go?** On the idle machine a normal task wakes with a median of 104 to 107 µs, a `SCHED_FIFO` one with 57 µs, and a normal task with 1 ns of timer slack with 53 to 68 µs. Explain the three numbers and say which part is not removable. *Where:* `wake` with and without `echo 1 > /proc/self/timerslack_ns`. *Approach:* slack is a deliberate delay for normal tasks only; the rest is the halted CPU and the host.

**Problem F. A long path.** A 10 Gbit/s link has a round trip of 30 ms. What window fills it, what does this VM's `tcp_rmem` and `tcp_wmem` allow one connection, and what would you change? *Where:* `sysctl`, then arithmetic. *Approach:* bandwidth-delay product; the lower of the receive and send limits decides.

**Problem G. A latency plan.** A 32-core, two-socket server (16 cores per node, NIC on node 1) must run four threads with a p99.9 under 50 µs and keep running the usual services. Write the **plan**: which cores are for the system, which for the latency threads, the boot line, the run-time steps, and the checks that show it worked. *Where:* a text file, then `lcheck.sh` on any Linux host to see what it reports. *Approach:* section 10; start from the NIC's node.

---

## 15. Answers

```text
--- Problem A. normal, nice -20 and FIFO 10 next to a burner on CPU 1, five runs each
normal, run 1                min    58  p50    66  p99   1022  p99.9   3964  max    3964  over100us 30
normal, run 2                min    65  p50    67  p99    632  p99.9   3937  max    3937  over100us 14
normal, run 3                min    58  p50    67  p99   1144  p99.9   3992  max    3992  over100us 48
normal, run 4                min    66  p50    68  p99    498  p99.9   3734  max    3734  over100us 21
normal, run 5                min    65  p50    67  p99    492  p99.9   2280  max    2280  over100us 17
nice -20, run 1              min    17  p50    67  p99   1911  p99.9   1962  max    1962  over100us 31
nice -20, run 2              min    65  p50    67  p99     93  p99.9    312  max     312  over100us 5
nice -20, run 3              min    65  p50    67  p99     95  p99.9   1843  max    1843  over100us 9
nice -20, run 4              min    58  p50    67  p99    121  p99.9    948  max     948  over100us 17
nice -20, run 5              min    65  p50    67  p99    173  p99.9   2354  max    2354  over100us 16
FIFO 10, run 1               min    15  p50    16  p99     24  p99.9     49  max      49  over100us 0
FIFO 10, run 2               min     8  p50    17  p99     36  p99.9    862  max     862  over100us 4
FIFO 10, run 3               min     8  p50    16  p99     76  p99.9   1718  max    1718  over100us 7
FIFO 10, run 4               min    15  p50    17  p99     34  p99.9     64  max      64  over100us 0
FIFO 10, run 5               min    10  p50    16  p99   2629  p99.9   3846  max    3846  over100us 25
```

**A.** The medians are **66 to 68 µs for normal**, **67 for `nice -20`** and **16 to 17 for `SCHED_FIFO 10`**. The ranges of the first two overlap completely and FIFO's do not touch them: **FIFO changes the median, `nice` does not.** For the p99, sorted: normal **492, 498, 632, 1,022, 1,144** (median **632**); `nice` **93, 95, 121, 173, 1,911** (median **121**); FIFO **24, 34, 36, 76, 2,629** (median **36**). `nice -20` and FIFO beat normal in four of five runs each; each has **one bad run** (1,911 and 2,629 µs) that priority cannot explain, which is the host. Wake-ups over 100 µs per 1,000: normal 14 to 48, `nice` 5 to 31, FIFO 0 to 25. **Conclusion:** FIFO is clearly better at the median and typically at the tail; `nice` improves the typical tail only; with the cost of FIFO (section 5.3) it is the right choice for one thread that blocks every iteration, and `nice` is the cheap one for the rest. Five runs cannot rank the bad runs: more runs, longer, are the next step.

```text
--- Problem B. the three commands, and what a task inside the fence sees
Cpus_allowed_list:      1
pid N's current affinity list: 1
```

**B.** The commands are `mkdir /sys/fs/cgroup/cpuset/fence`, `echo 1 > .../fence/cpuset.cpus`, `echo 0 > .../fence/cpuset.mems`, then `echo $$ > .../fence/tasks` from the shell to be fenced. The shell reports **`Cpus_allowed_list: 1`** and `taskset -pc` agrees. Set `cpuset.mems` as well: a task cannot enter a cpuset with no memory nodes (from memory, an error). In version 2 the equivalent is `+cpuset` in the parent's `cgroup.subtree_control`, the same two files, and `cgroup.procs`; an **isolated partition** (`cpuset.cpus.partition`) also takes the CPUs out of load balancing. Remove the directory with `rmdir` after the tasks have left.

```text
--- Problem C. how much memory the TLB can map (arithmetic, entry count from memory)
2048 entries of 4 KB pages reach 8 MB
2048 entries of 2 MB pages reach 4 GB
2048 entries of 1 GB pages reach 2048 GB
buffer    4 MB needs    1024 pages of 4 KB or     2 pages of 2 MB
buffer   64 MB needs   16384 pages of 4 KB or    32 pages of 2 MB
buffer  512 MB needs  131072 pages of 4 KB or   256 pages of 2 MB
buffer 2048 MB needs  524288 pages of 4 KB or  1024 pages of 2 MB
```

**C.** **8 MB, 4 GB and 2,048 GB** of reach. The 4 MB buffer is far inside 8 MB and fits the L2 cache: **no effect, as measured** (57.3, 55.0, 53.6 ns). At 64 MB the buffer is eight times the reach, but the **260 MB L3** holds the data, so only the walk is saved: **6 to 8 percent**. At 512 MB and 2,048 MB the data comes from DRAM **and** every access misses the TLB: **21 and 34 percent**. The prediction "starts above 8 MB, grows with size" matches. In a guest the nested walk makes the gain larger than on bare metal for the same working set (from memory).

**D.** An ordinary task gets the **throttle gap: 5.3 percent** of the CPU (measured: 32 ticks of 600) and wakes after **71 ms median** (maximum 217 ms). A **`SCHED_FIFO 40` task gets nothing at all**: it was **3.5 to 4.0 seconds late** on every wake-up and could not even be killed until the spinner was. A **`SCHED_FIFO 99` task** is a second runaway: with equal priority it waits behind the first in arrival order, and it too is blocked by the throttle for 50 ms each second (a higher FIFO task here, 60, had a p99 of 33 ms). To make the mistake safe: keep the throttle on (never `-1`), reserve **CPU 0** for the administrator and run the shell that kills it there, set **`RLIMIT_RTTIME`** (`ulimit -R`) so the kernel kills a real-time task that runs too long without blocking, and give the group a `cpu.rt_runtime_us` budget if the kernel has real-time group scheduling.

**E.** The **default timer slack of 50 µs** lets the kernel wake a normal task up to 50 µs late; removing it (1 ns) took the median from **104 to 107 µs down to 53 to 68 µs**, and `SCHED_FIFO` has no slack, hence **57 µs** (and 14 to 17 µs when the CPU is busy). What does not go away is the **cost of the halted CPU** (a VM exit here, a C-state on bare metal: section 4, 66 to 69 µs with the CPU kept awake) and the **host's tail**. The three figures separate three causes: slack, halt, host.

```text
--- Problem F. window needed for 10 Gbit/s at 30 ms (arithmetic)
bandwidth-delay product: 37.5 MB
this VM's tcp_rmem maximum: 33.6 MB, so one connection could reach 8.9 Gbit/s at 30 ms
with the 128 KB default window one connection reaches 35.0 Mbit/s
```

**F.** **37.5 MB** must be in flight. The receive side allows **33,554,432 bytes: 8.9 Gbit/s at 30 ms**, close but short; the **send side is the real limit**: `tcp_wmem` tops out at **4,194,304 bytes**, and 4,194,304 times 8 divided by 0.030 s is **1.12 Gbit/s**. With the 128 KB default window one connection reaches **35.0 Mbit/s**. Change: raise the maximum of `tcp_rmem` and `tcp_wmem` to at least **40 MB** (and `rmem_max` and `wmem_max` if applications set their own buffers), leave autotuning alone, use a congestion control that copes with the path (`bbr` here) with `fq`, and check with `ss -ti` that the window, not the application, is the limit.

**G.** One possible plan (**from memory; it was not run**). **Cores:** node 1 owns the NIC, so the latency threads go on **four cores of node 1** (say 20 to 23); the system takes **cores 0, 1 and 16, 17** (two per node, with their SMT siblings if any) and the other services take everything else. **Boot line:** `isolcpus=domain,managed_irq,20-23 nohz_full=20-23 rcu_nocbs=20-23 irqaffinity=0-1,16-17 processor.max_cstate=1 intel_idle.max_cstate=1 nosoftlockup`, plus `default_hugepagesz=1G hugepagesz=1G hugepages=8`. **Run time:** NIC IRQs to cores 16 and 17; `irqbalance` banning 20 to 23; the services' slice `AllowedCPUs=` the others; the threads pinned one per core with `SCHED_FIFO` 80 and `mlockall`, buffers from the 1 GB pages with the memory policy on node 1; governor `performance`; `kernel.numa_balancing=0`, `vm.swappiness=1`, THP `madvise`. **Checks:** `lcheck.sh` all OK (`irq_vs_isolated` above all), `jitter` on each isolated core showing only a few microseconds, `wake` histograms at the real load for hours, `/proc/interrupts` flat on cores 20 to 23, `numastat -p` showing the threads' memory on node 1. Each box ticked is something that would have been a silent failure.

---

## 16. Cheat sheet

```text
METHOD     goal as a percentile | baseline under load | one change | 3+ runs | gap > spread | state the price | soak
TOOLS      wake (lateness of a timed wake-up) | jitter (stolen CPU time) | perf sched, cyclictest, turbostat | /proc/interrupts, softirqs
SOURCES    other task | idle exit | timer slack | frequency | IRQ/softirq | tick | faults/reclaim | TLB/cache | NUMA | throttle | host
PLACE      taskset -c N CMD | chrt | cpuset: mkdir, cpuset.cpus, cpuset.mems, tasks | systemd AllowedCPUs= | numactl --cpunodebind --membind
ISOLATE    boot: isolcpus=domain,managed_irq,L nohz_full=L rcu_nocbs=L irqaffinity=H | cpuset v2 partition=isolated | check with lcheck.sh
IDLE       idle exit ~100 us here: SCHED_IDLE spinner (66-69 us) | max_cstate=1, idle=poll | /dev/cpu_dma_latency (no driver on a VM: no effect)
FREQ       governor performance, turbo and thermal checks | none in a guest
TIMERS     slack 50 us default: prctl(PR_SET_TIMERSLACK,1) or /proc/PID/timerslack_ns | FIFO/RR/DEADLINE have none
RT         SCHED_FIFO/RR 1-99, DEADLINE | throttle 950000/1000000 us: 5% to others, ALL rt blocked 50 ms | never -1 | RLIMIT_RTTIME
           block every loop | mlockall | pre-fault | PTHREAD_PRIO_INHERIT | PREEMPT_RT mainline 6.12 | keep CPU 0 free
IRQ        /proc/interrupts, /proc/softirqs | /proc/irq/N/smp_affinity_list | irqaffinity= | irqbalance banned list | managed_irq
NET QUEUES RSS (hw hash, queue per IRQ) | RPS/RFS (sw, IPI; hurt 47->31 Gbit/s on 2 CPUs) | XPS | ethtool -l/-g/-k/-c | busy_poll
NIC        ring (latency: small, throughput: large) | GRO/TSO/GSO on for CPU, off for lowest delay | coalescing | XDP/DPDK bypass
HUGE       reach = entries x page size (2048 x 4 KB = 8 MB; x 2 MB = 4 GB) | THP madvise/never | vm.nr_hugepages, hugepages= at boot
           read back HugePages_Total | 34% less time at 2 GB, 0 at 4 MB | cgroup hugetlb
FAULTS     mlockall, MAP_POPULATE, swappiness, min_free_kbytes, dirty ratios, numa_balancing=0
NUMA       first touch | thread + memory + NIC on one node | numactl -H, numastat -p | interleave for shared bandwidth
TCP        window >= BDP = bandwidth x RTT | autotune: leave alone, raise tcp_rmem/tcp_wmem max | SO_RCVBUF disables autotune
           slow_start_after_idle=0 | notsent_lowat small | fq + bbr | somaxconn | netdev_max_backlog
PROFILES   tuned-adm profile latency-performance/network-latency/cpu-partitioning | read /usr/lib/tuned first
CLOUD      guest can set affinity, fence, RT, IRQs | host owns steal, C-states, frequency | dedicated instance for hard tails
```

---

## 17. Sources

- `sched(7)`, `sched_setscheduler(2)`, `sched_setaffinity(2)`, `chrt(1)`, `taskset(1)`, `prctl(2)` (`PR_SET_TIMERSLACK`), `cpuset(7)`, `numa(7)`, `numactl(8)`, `mlockall(2)`, `getrlimit(2)` (`RLIMIT_RTTIME`). <https://man7.org/linux/man-pages/man7/sched.7.html>
- Kernel documentation: *Real-Time group scheduling* (the throttle), *Reducing OS jitter due to per-cpu kthreads*, *NO_HZ: reducing scheduling-clock ticks*, *SMP IRQ affinity*, *Scaling in the Linux Networking Stack* (RSS, RPS, RFS, XPS), *Transparent Hugepage Support*, *HugeTLB Pages*, *PM Quality of Service Interface*, *CPU idle time management*, *CPU performance scaling*. <https://docs.kernel.org/>
- The Real-Time Linux wiki and the `rt-tests` suite (`cyclictest`, `hackbench`) for measuring latency on `PREEMPT_RT`. <https://wiki.linuxfoundation.org/realtime/start>
- Red Hat, *Optimizing RHEL for Real Time for low latency operation*, and the `tuned` project, for the profiles and the boot-line patterns. <https://tuned-project.org/>
- Ulrich Drepper, *What Every Programmer Should Know About Memory* (2007): TLBs, huge pages and NUMA. <https://akkadia.org/drepper/cpumemory.pdf>
- Brendan Gregg, *Systems Performance*, 2nd edition (2020): CPU, memory and network methodology, scheduler latency and `perf sched`. <https://www.brendangregg.com/linuxperf.html>
- RFC 7323, *TCP Extensions for High Performance* (window scaling), for the bandwidth-delay product and why a window caps a connection.
- Michael Kerrisk, *The Linux Programming Interface* (2010), chapters on real-time scheduling, timers and memory locking.
