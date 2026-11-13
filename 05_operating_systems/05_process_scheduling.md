# OS 05: Scheduling in Linux

*Part of the [Operating systems plan](00_plan.md) · Time: about 7 hours (4.5 reading and experiments, 2.5 problems) · You need: [OS 04](04_cpu_scheduling_algorithms.md) (the algorithms, MLFQ, lottery and stride, RM and EDF), [OS 02](02_processes.md) (context switches) and [OS 03](03_threads.md) (threads)*

OS 04 taught the algorithms. This chapter is about the one scheduler you will be asked about by name: Linux's. Interviewers at systems companies ask it in a few fixed forms. How does Linux decide which thread runs next? What replaced CFS, and why? What does `nice` actually change? Why does my latency-sensitive thread still see 1 ms spikes when the machine is half idle? When would you use `SCHED_FIFO`, and what stops it from hanging the box? What is `SCHED_DEADLINE`? What do containers' CPU limits do to a process? This chapter answers each from the kernel's own documentation and source, and checks every claim it can on a real kernel with a program you can run.

By the end you can:

- name the scheduling classes in priority order and say which policy maps to which class;
- explain CFS's virtual runtime and EEVDF's lag, eligibility and virtual deadline, and work one EEVDF decision by hand;
- convert nice values to weights and predict CPU shares (measured within 0.1 percentage point here);
- say what a time slice is on a real machine, and why a 0.2 ms slice request changed nothing on this one;
- explain wake-up latency: timer slack, wake-up preemption, the idle-CPU penalty, and which fixes work (measured);
- explain per-CPU run queues and load balancing, and group scheduling with cgroups (shares and quotas, measured);
- use `SCHED_DEADLINE` and predict what admission control accepts;
- describe the preemption models (none, voluntary, full, lazy, RT), `sched_ext`, and the CPU-isolation checklist for low-latency work.

**Where the numbers come from.** Every program in this chapter was compiled with GCC 13.3 (`-O2 -Wall -Wextra`) and run on a Linux 6.18.44 kernel inside a Firecracker micro-VM with 2 vCPUs (Intel Xeon at 2.30 GHz), as root. The scheduler's rules are quoted or paraphrased from the kernel documentation and from `kernel/sched/core.c` and `kernel/sched/fair.c` (fetched; linked in the Sources). Facts from general knowledge are marked "from memory". What this machine can show is limited: 2 CPUs, no `NO_HZ_FULL`, no `sched_ext` and no schedstats in its kernel build, `debugfs` scheduler files not readable, and a hypervisor that sometimes pauses the whole virtual CPU. The text says where each limit matters.

**Data and timing.** Measurements of shares and CPU time come from the kernel's own accounting (`/proc/<pid>/sched`), which is exact to the nanosecond for what it counts. Latency measurements are wall-clock and noisy on a shared cloud host; those ran twice and are shown as run 1 / run 2 (two complete repetitions). Tail figures (99.9th percentile, maximum) from 5,000 samples move between runs; read them as orders of magnitude.

---

## 1. The picture: classes, policies and per-CPU run queues

Linux does not have one scheduling algorithm. It has a stack of **scheduling classes**, each with its own algorithm, and it always asks the highest class first. In priority order (the order is from memory; the policies and their semantics are in [sched(7)](https://man7.org/linux/man-pages/man7/sched.7.html)):

| Class | Policies (what a program asks for) | Algorithm | Section |
|---|---|---|---|
| stop | none (kernel-internal, for CPU hotplug and migration) | runs one kernel thread | not covered |
| deadline | `SCHED_DEADLINE` | EDF with a constant-bandwidth server | 8 |
| real-time | `SCHED_FIFO`, `SCHED_RR`, priorities 1 to 99 | fixed priority; RR adds a quantum | 7 |
| fair | `SCHED_OTHER` (also called `SCHED_NORMAL`), `SCHED_BATCH`, `SCHED_IDLE` | EEVDF since 6.6, CFS before | 2 to 6 |
| ext | the BPF scheduler when one is loaded | whatever the BPF program implements | 9 |
| idle | none (the per-CPU idle task) | runs when nothing else can | not covered |

When a CPU needs a task, the kernel asks each class in this order for its best runnable task and takes the first answer. So any runnable real-time task beats every normal task, which is OS 04's multilevel queue with strict priority between the queues. This is also why one runaway `SCHED_FIFO` loop could hang a CPU, and section 7 shows what stops it.

**Per-CPU run queues.** Each CPU has its own run queue (`struct rq`) holding one sub-queue per class. A CPU picks from its own queue without a global lock, which is OS 04's "per-CPU queues scale", and a **load balancer** moves tasks between queues (section 5).

**What this machine runs.** Everything below was measured on this configuration:

```sh
#!/bin/sh
# l0_env.sh: what scheduler is this machine running? (read-only)
echo "kernel:                $(uname -r)"
echo "tick rate:             $(zcat /proc/config.gz 2>/dev/null | grep '^CONFIG_HZ=' | cut -d= -f2) Hz"
echo "preemption at boot:    $(dmesg 2>/dev/null | grep -o 'Dynamic Preempt: [a-z]*' | head -1)"
echo "sched_ext built in:    $(zcat /proc/config.gz 2>/dev/null | grep -c '^CONFIG_SCHED_CLASS_EXT=y')"
echo "NO_HZ_FULL built in:   $(zcat /proc/config.gz 2>/dev/null | grep -c '^CONFIG_NO_HZ_FULL=y')"
echo "CPUs:                  $(nproc)"
echo "base slice (se.slice): $(grep se.slice /proc/self/sched | awk '{print $3}') ns"
echo "SCHED_RR quantum:      $(cat /proc/sys/kernel/sched_rr_timeslice_ms) ms"
echo "RT period / runtime:   $(cat /proc/sys/kernel/sched_rt_period_us) / $(cat /proc/sys/kernel/sched_rt_runtime_us) us"
echo "autogroup:             $(cat /proc/sys/kernel/sched_autogroup_enabled)"
echo "timer slack (normal):  $(cat /proc/self/timerslack_ns) ns"
```

```text
kernel:                6.18.44-fc-v445
tick rate:             250 Hz
preemption at boot:    Dynamic Preempt: none
sched_ext built in:    0
NO_HZ_FULL built in:   0
CPUs:                  2
base slice (se.slice): 1400000 ns
SCHED_RR quantum:      100 ms
RT period / runtime:   1000000 / 950000 us
autogroup:             1
timer slack (normal):  50000 ns
```

The base slice of 1.4 ms is the kernel's default formula at work: `fair.c` gives "0.70 msec * (1 + ilog(ncpus))", and with 2 CPUs `ilog2(2) = 1`, so 0.7 ms times 2. The tick rate of 250 Hz means a timer interrupt every 4 ms; section 3 shows why that matters more than the slice.

---

## 2. The fair class: from CFS to EEVDF

### 2.1 CFS: virtual runtime

CFS (the Completely Fair Scheduler, default from 2.6.23 until 6.6) models what the [kernel documentation](https://www.kernel.org/doc/html/latest/scheduler/sched-design-CFS.html) calls an ideal multitasking CPU that runs every task "at precise equal speed, in parallel". Real CPUs run one task at a time, so CFS tracks how far each task is behind that ideal:

- Each task has a **virtual runtime** `vruntime`. When a task runs for `t` nanoseconds, its `vruntime` grows by `t * 1024 / weight`. A nice-0 task (weight 1024) ages at real speed; a heavier task ages slower, so it gets more real time for the same virtual time.
- Runnable tasks sit in a red-black tree ordered by `vruntime` (the kernel rbtree is DSA Trees 13). CFS runs the leftmost task, the one that has received the least weighted service.
- A newly woken or new task is placed near the queue's minimum `vruntime`, so a task that slept for an hour cannot claim an hour of CPU.

This is OS 04's stride scheduling (section 7 there): `vruntime` is the pass value and `1024 / weight` is the stride.

### 2.2 Nice values are weights

`nice` runs from -20 to 19. The kernel converts it to a weight through a fixed table in `kernel/sched/core.c`, whose comment says nice levels "are multiplicative, with a gentle 10% change for every nice level changed" and that "we use a multiplier of 1.25". Nice 0 is 1024; nice -5 is 3121; nice 5 is 335; nice 19 is 15. Two tasks on one CPU split it in the ratio of their weights. The program below runs one CPU-bound process per nice value given, all pinned to CPU 1 and all stopping at the same instant, and reads the kernel's own accounting:

```c
/* util.h: helpers shared by the OS 05 programs (Linux only). */
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static inline double now(void)                              /* seconds, monotonic */
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static inline void pin(int cpu)                             /* run only on this CPU */
{
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(cpu, &s);
    if (sched_setaffinity(0, sizeof s, &s) != 0) { perror("sched_setaffinity"); exit(1); }
}

/* One numeric field of /proc/<pid>/sched, for example "se.vruntime" or "nr_switches". */
static inline double sched_field(pid_t pid, const char *name)
{
    char path[64], line[256];
    snprintf(path, sizeof path, "/proc/%d/sched", (int)pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    double v = -1;
    size_t n = strlen(name);
    while (fgets(line, sizeof line, f))
        if (strncmp(line, name, n) == 0 && (line[n] == ' ' || line[n] == ':')) {
            char *colon = strchr(line, ':');
            if (colon) v = atof(colon + 1);
            break;
        }
    fclose(f);
    return v;
}

/* A line of /proc/self/status such as "voluntary_ctxt_switches". */
static inline long status_field(const char *name)
{
    char line[256];
    FILE *f = fopen("/proc/self/status", "r");
    long v = -1;
    size_t n = strlen(name);
    while (f && fgets(line, sizeof line, f))
        if (strncmp(line, name, n) == 0) { v = atol(line + n + 1); break; }
    if (f) fclose(f);
    return v;
}
```

```c
/* l1_weights.c: CPU-bound processes with different nice values share one CPU by weight.
   Usage: l1_weights SECONDS NICE...   (all children are pinned to CPU 1) */
#include "util.h"
#include <sys/resource.h>
#include <sys/wait.h>

static const int weight[40] = {                       /* kernel/sched/core.c, nice -20..19 */
    88761, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916,
     9548,  7620,  6100,  4904,  3906,  3121,  2501,  1991,  1586,  1277,
     1024,   820,   655,   526,   423,   335,   272,   215,   172,   137,
      110,    87,    70,    56,    45,    36,    29,    23,    18,    15 };

int main(int argc, char **argv)
{
    double secs = atof(argv[1]);
    int n = argc - 2, go[2], res[2];
    if (pipe(go) || pipe(res)) return 1;
    for (int i = 0; i < n; i++) {
        int nice_v = atoi(argv[2 + i]);
        if (fork() == 0) {
            pin(1);
            setpriority(PRIO_PROCESS, 0, nice_v);
            double end;                                    /* the same end time for every child */
            close(go[1]);
            if (read(go[0], &end, sizeof end) != sizeof end) _exit(1);
            while (now() < end) { }                        /* burn CPU */
            pid_t me = getpid();
            char line[160];
            int len = snprintf(line, sizeof line, "%d %.1f %.1f %.0f\n", nice_v,
                               sched_field(me, "se.sum_exec_runtime"),
                               sched_field(me, "se.vruntime"),
                               sched_field(me, "se.load.weight"));
            if (write(res[1], line, len) != len) _exit(1);
            _exit(0);
        }
    }
    close(go[0]);
    double end = now() + secs;
    for (int i = 0; i < n; i++)                            /* start them all together */
        if (write(go[1], &end, sizeof end) != sizeof end) return 1;
    close(res[1]);
    for (int i = 0; i < n; i++) wait(NULL);

    int nv[16]; double run[16], vr[16], lw[16], total_run = 0, total_w = 0;
    FILE *f = fdopen(res[0], "r");
    for (int i = 0; i < n; i++) {
        if (fscanf(f, "%d %lf %lf %lf", &nv[i], &run[i], &vr[i], &lw[i]) != 4) return 1;
        total_run += run[i];
        total_w += weight[nv[i] + 20];
    }
    printf("nice  weight  load.weight  CPU ms  share   predicted  vruntime ms\n");
    for (int i = 0; i < n; i++)
        printf("%4d  %6d  %11.0f  %6.0f  %5.1f%%  %8.1f%%  %11.1f\n", nv[i], weight[nv[i] + 20],
               lw[i], run[i], 100 * run[i] / total_run, 100 * weight[nv[i] + 20] / total_w, vr[i]);
    return 0;
}
```

Run 1 and run 2 with nice -5, 0 and 5:

```text
nice  weight  load.weight  CPU ms  share   predicted  vruntime ms
   0    1024      1048576     905   22.8%      22.9%       1132.3
  -5    3121      3195904    2764   69.7%      69.7%       1134.6
   5     335       343040     299    7.5%       7.5%       1140.9
```

```text
nice  weight  load.weight  CPU ms  share   predicted  vruntime ms
   0    1024      1048576     908   22.8%      22.9%       2049.9
  -5    3121      3195904    2775   69.7%      69.7%       2051.8
   5     335       343040     299    7.5%       7.5%       2054.0
```

What to read from it:

- **Shares match the weights to within 0.1 percentage point**: 3121 : 1024 : 335 predicts 69.7, 22.9 and 7.5 percent.
- **The `vruntime` values are nearly equal** even though the CPU times differ by a factor of nine. That is the fairness invariant: the scheduler keeps the weighted service equal, not the real service.
- **`load.weight` is the table value times 1024** (3121 shows as 3,195,904). On 64-bit kernels the weights carry 10 extra bits of fixed-point precision (from memory; the shift is visible in the numbers).

### 2.3 EEVDF: lag, eligibility and a virtual deadline

CFS had no way to say "this task needs the CPU soon, but not much of it". Nice changes how much, not how soon. [EEVDF](https://www.kernel.org/doc/html/latest/scheduler/sched-eevdf.html) (Earliest Eligible Virtual Deadline First, a 1995 algorithm) replaced CFS's picking logic in Linux 6.6 and adds exactly that. It keeps weighted virtual time and adds three ideas, all stated in comments in `kernel/sched/fair.c`:

1. **V, the queue's virtual time**, is "the weighted average of all entity virtual runtimes": `V = sum(w_i * v_i) / sum(w_i)`.
2. **Lag** is how much service a task is owed: `lag_i = w_i * (V - v_i)`. Positive lag means it has received less than its share; the lags of the queued tasks sum to zero. A task is **eligible** "once it received less service than it ought to have, eg. lag >= 0", which is the same as `v_i <= V`.
3. Each task has a **virtual deadline** `vd_i = v_i + r_i / w_i` (the source writes "vd_i = ve_i + r_i / w_i"), where `r_i` is the task's requested slice, the 1.4 ms base slice unless the task asked for another. In code, `se->deadline = se->vruntime + calc_delta_fair(se->slice, se)`, which scales the slice by `1024 / weight`.

**The rule: among eligible tasks, run the one with the earliest virtual deadline.** Eligibility gives fairness (nobody runs ahead of their share for long); the deadline gives latency control (a short slice gives an early deadline, so a task that asks for small, frequent pieces gets picked sooner without getting more CPU in total).

A worked example (Problem B prints it). Three tasks: A and B at nice 0 (weight 1024) with `v` = 10 and 12 ms, C at nice 5 (weight 335) with `v` = 11 ms. Then `V = (1024*10 + 1024*12 + 335*11) / 2383 = 11.0`. A (10 <= 11) and C (11 <= 11) are eligible; B is not. Deadlines: A `10 + 1.4 = 11.4`, C `11 + 1.4 * 1024 / 335 = 15.28`. **A runs.** If C asks for a 0.2 ms slice its deadline becomes `11 + 0.2 * 1024 / 335 = 11.61`, still later than A's 11.4; if A also asks for 20 ms, A's deadline moves to 30 and **C runs**.

**Sleeping and lag.** If a task could sleep away a negative lag, a task could game the scheduler by sleeping at the end of every slice. According to the [LWN account of completing EEVDF](https://lwn.net/Articles/969062), an ineligible task that sleeps stays on the queue marked for "deferred dequeue" until its lag decays to zero over virtual time; positive lag is kept until the task runs again.

**Custom slices.** The same LWN article describes letting any task request its slice through `sched_setattr()`'s `sched_runtime` field (in nanoseconds, 100 µs to 100 ms). The man page still documents that field for `SCHED_DEADLINE` only. On this 6.18 kernel the request is accepted and shows up as `se.slice` (section 3 measures it); the kernel release that added it is not stated in the sources fetched (LWN tags the article 6.12).

---

## 3. What a time slice really is on this machine

A slice of 1.4 ms suggests that two CPU-bound tasks on one CPU swap every 1.4 ms. The program below puts one or more spinners on CPU 1. Each one watches the clock and logs every gap of more than 20 µs (it was off the CPU). At the end it also reads the kernel's count of involuntary switches, so you can compare what the task saw with what the scheduler did. Spinner 0 can request a custom slice.

```c
/* l3_slice.c: how long does a CPU-bound task run before the scheduler switches?
   NSPIN spinners share CPU 1. Each watches the clock; a jump of more than 20 us means it
   was off the CPU (switched out by Linux, or the whole virtual CPU paused by the host).
   Usage: l3_slice SECONDS NSPIN [SLICE_US for spinner 0, via sched_setattr] */
#include "util.h"
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/wait.h>

struct sched_attr {                                   /* from sched_setattr(2); glibc has no wrapper */
    uint32_t size, sched_policy;
    uint64_t sched_flags;
    int32_t sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime, sched_deadline, sched_period;
};

static int cmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv)
{
    if (argc < 3) return 1;
    double secs = atof(argv[1]);
    int nspin = atoi(argv[2]);
    long slice_us = argc > 3 ? atol(argv[3]) : 0;
    int go[2];
    if (pipe(go)) return 1;
    for (int k = 0; k < nspin; k++)
        if (fork() == 0) {
            pin(1);
            if (k == 0 && slice_us > 0) {             /* SCHED_OTHER with a custom slice */
                struct sched_attr a = { .size = sizeof a, .sched_policy = SCHED_OTHER,
                                        .sched_runtime = slice_us * 1000 };
                if (syscall(SYS_sched_setattr, 0, &a, 0) != 0) { perror("sched_setattr"); _exit(1); }
            }
            double end;
            close(go[1]);
            if (read(go[0], &end, sizeof end) != sizeof end) _exit(1);
            static double runs[100000];
            int nr = 0;
            double start = now(), last = start, t;
            while ((t = now()) < end) {
                if (t - last > 20e-6) {               /* we were off the CPU */
                    if (nr < 100000) runs[nr++] = (last - start) * 1e6;
                    start = t;
                }
                last = t;
            }
            qsort(runs, nr, sizeof runs[0], cmp);
            usleep(k * 100000);                       /* print in order */
            pid_t me = getpid();
            double cpu_ms = sched_field(me, "se.sum_exec_runtime");
            double inv = sched_field(me, "nr_involuntary_switches");
            printf("spinner %d%s: se.slice %5.0f us | gaps seen %4d, median run %5.0f us, 90th %5.0f us"
                   " | kernel: %4.0f preemptions, %5.0f us of CPU per preemption\n",
                   k, (k == 0 && slice_us) ? " (custom)" : "         ", sched_field(me, "se.slice") / 1000,
                   nr, nr ? runs[nr / 2] : 0, nr ? runs[nr * 9 / 10] : 0, inv,
                   inv > 0 ? 1000 * cpu_ms / inv : 0);
            fflush(stdout);                           /* _exit does not flush stdio */
            _exit(0);
        }
    close(go[0]);
    double end = now() + secs;
    for (int k = 0; k < nspin; k++)
        if (write(go[1], &end, sizeof end) != sizeof end) return 1;
    while (wait(NULL) > 0) { }
    return 0;
}
```

Each run below is: one spinner alone; two spinners; two with spinner 0 asking for 200 µs; two with spinner 0 asking for 20 ms. Run 1:

```text
spinner 0         : se.slice  1400 us | gaps seen  494, median run  2447 us, 90th 17467 us | kernel:    6 preemptions, 499100 us of CPU per preemption
spinner 0         : se.slice  1400 us | gaps seen  621, median run  2890 us, 90th  3984 us | kernel:  375 preemptions,  3985 us of CPU per preemption
spinner 1         : se.slice  1400 us | gaps seen  631, median run  2899 us, 90th  3984 us | kernel:  377 preemptions,  3964 us of CPU per preemption
spinner 0 (custom): se.slice   200 us | gaps seen  671, median run  1750 us, 90th  3984 us | kernel:  375 preemptions,  3987 us of CPU per preemption
spinner 1         : se.slice  1400 us | gaps seen  662, median run  1722 us, 90th  3984 us | kernel:  373 preemptions,  4015 us of CPU per preemption
spinner 0 (custom): se.slice 20000 us | gaps seen  387, median run  3984 us, 90th  3985 us | kernel:  374 preemptions,  4013 us of CPU per preemption
spinner 1         : se.slice  1400 us | gaps seen  394, median run  3984 us, 90th  3985 us | kernel:  375 preemptions,  3997 us of CPU per preemption
```

Run 2:

```text
spinner 0         : se.slice  1400 us | gaps seen  573, median run  1356 us, 90th 11975 us | kernel:    9 preemptions, 330733 us of CPU per preemption
spinner 0         : se.slice  1400 us | gaps seen  404, median run  3983 us, 90th  3984 us | kernel:  375 preemptions,  3997 us of CPU per preemption
spinner 1         : se.slice  1400 us | gaps seen  390, median run  3983 us, 90th  3984 us | kernel:  376 preemptions,  3989 us of CPU per preemption
spinner 0 (custom): se.slice   200 us | gaps seen  488, median run  3977 us, 90th  3984 us | kernel:  375 preemptions,  3984 us of CPU per preemption
spinner 1         : se.slice  1400 us | gaps seen  489, median run  3976 us, 90th  3984 us | kernel:  375 preemptions,  3990 us of CPU per preemption
spinner 0 (custom): se.slice 20000 us | gaps seen  482, median run  3976 us, 90th  3983 us | kernel:  375 preemptions,  4001 us of CPU per preemption
spinner 1         : se.slice  1400 us | gaps seen  483, median run  3977 us, 90th  3983 us | kernel:  374 preemptions,  4006 us of CPU per preemption
```

What it shows:

- **The kernel preempted each spinner every 4 ms, whatever slice was requested.** The "CPU per preemption" column is close to 3,990 µs for the 1.4 ms default, for a 200 µs request and for a 20 ms request. 4 ms is one tick at 250 Hz. On this kernel, a running task's slice is checked at the tick (and when another task wakes), not at the instant it expires, so the effective slice is rounded up to a tick. A high-resolution tick for the fair class exists as a scheduler feature that is off by default (from memory; `debugfs` features were not readable here).
- **Why a 20 ms request did not give 20 ms runs:** with the 20 ms slice, spinner 0's deadline is far in the future. At every tick the other spinner is eligible with an earlier deadline, so the scheduler switches to it. A long slice only means "my deadline is late", which makes the task easier to preempt, not harder. (This is my reading of the rule in section 2.3, consistent with the numbers; it was not traced inside the kernel.)
- **What the task sees is noisier than what the kernel did.** Alone on the CPU, the spinner saw hundreds of gaps but the kernel preempted it only a few times: the other gaps are the hypervisor pausing the virtual CPU. On a VM, measure scheduling from the kernel's counters (`/proc/<pid>/sched`, `/proc/<pid>/status`), not from gaps your program sees.

---

## 4. Wake-up latency

Most latency-sensitive work is not CPU-bound: a control loop, a packet handler, an audio callback. It sleeps, wakes on a timer or an event, does a little work and sleeps again. What matters is how late it runs after it should have woken. The program below wakes every 1 ms on an absolute timer on CPU 1 and records the lateness, with 0 or 2 CPU-bound neighbours on the same CPU, in four modes: plain `SCHED_OTHER` (`other`), the same with timer slack set to 1 ns (`noslack`), a 100 µs custom slice (`slice`, also without slack) and `SCHED_FIFO` priority 50 (`fifo`).

```c
/* l4_wakeup.c: how late does a periodic task wake up when CPU-bound tasks share its CPU?
   A task on CPU 1 sleeps until each 1 ms boundary (absolute clock_nanosleep) and records
   how late it actually ran. Usage: l4_wakeup HOGS MODE   MODE: other | noslack | slice | fifo */
#include "util.h"
#include <signal.h>
#include <stdint.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>

struct sched_attr {
    uint32_t size, sched_policy;
    uint64_t sched_flags;
    int32_t sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime, sched_deadline, sched_period;
};

static int cmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv)
{
    if (argc != 3) return 1;
    int hogs = atoi(argv[1]);
    const char *mode = argv[2];
    pid_t kids[8];
    for (int i = 0; i < hogs; i++)
        if ((kids[i] = fork()) == 0) { pin(1); for (;;) { } }      /* CPU-bound neighbours */

    pin(1);
    struct sched_attr a = { .size = sizeof a, .sched_policy = SCHED_OTHER };
    if (strcmp(mode, "other") != 0 && strcmp(mode, "fifo") != 0)
        prctl(PR_SET_TIMERSLACK, 1UL, 0, 0, 0);                    /* 1 ns instead of 50 us */
    if (strcmp(mode, "slice") == 0) a.sched_runtime = 100 * 1000;  /* ask for a 100 us slice */
    if (strcmp(mode, "fifo") == 0) { a.sched_policy = SCHED_FIFO; a.sched_priority = 50; }
    if (syscall(SYS_sched_setattr, 0, &a, 0) != 0) { perror("sched_setattr"); return 1; }

    enum { N = 5000 };
    static double late[N];
    struct timespec t, w;
    clock_gettime(CLOCK_MONOTONIC, &t);
    for (int i = 0; i < N; i++) {
        t.tv_nsec += 1000000;                                      /* next 1 ms boundary */
        if (t.tv_nsec >= 1000000000) { t.tv_nsec -= 1000000000; t.tv_sec++; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL);
        clock_gettime(CLOCK_MONOTONIC, &w);
        late[i] = (w.tv_sec - t.tv_sec) * 1e6 + (w.tv_nsec - t.tv_nsec) / 1e3;
        if (late[i] > 1000)                                        /* skip missed periods */
            t = w;
    }
    for (int i = 0; i < hogs; i++) kill(kids[i], SIGKILL);
    while (wait(NULL) > 0) { }
    qsort(late, N, sizeof late[0], cmp);
    printf("%d hogs, %-7s: wake-up lateness us: median %6.1f  p99 %7.1f  p99.9 %7.1f  max %7.1f\n",
           hogs, mode, late[N / 2], late[N * 99 / 100], late[N * 999 / 1000], late[N - 1]);
    return 0;
}
```

Run 1:

```text
0 hogs, other  : wake-up lateness us: median   81.8  p99   146.4  p99.9   188.6  max   326.3
0 hogs, noslack: wake-up lateness us: median   29.7  p99   125.2  p99.9   866.2  max  6249.6
2 hogs, other  : wake-up lateness us: median   66.3  p99   987.4  p99.9  6835.9  max  7999.6
2 hogs, noslack: wake-up lateness us: median   16.1  p99   984.5  p99.9  3201.6  max  8000.0
2 hogs, slice  : wake-up lateness us: median   15.6  p99   990.4  p99.9  3991.3  max  7997.2
2 hogs, fifo   : wake-up lateness us: median   17.2  p99    48.3  p99.9   831.7  max  1940.0
```

Run 2:

```text
0 hogs, other  : wake-up lateness us: median   83.4  p99   159.4  p99.9   905.1  max  1869.8
0 hogs, noslack: wake-up lateness us: median   30.2  p99   103.0  p99.9  1089.4  max  2340.9
2 hogs, other  : wake-up lateness us: median   65.0  p99   991.5  p99.9  4903.8  max  7997.1
2 hogs, noslack: wake-up lateness us: median   16.0  p99   960.7  p99.9  4990.5  max  8004.3
2 hogs, slice  : wake-up lateness us: median   15.6  p99   986.5  p99.9  6589.3  max  7357.5
2 hogs, fifo   : wake-up lateness us: median   15.6  p99    56.1  p99.9   280.9  max  2023.1
```

Read it row by row:

- **Timer slack.** A normal thread's timers may fire up to 50 µs late on purpose (`/proc/self/timerslack_ns` is 50000), so the kernel can batch wake-ups and save power. That is the difference between the `other` and `noslack` medians. The [`PR_SET_TIMERSLACK` man page](https://man7.org/linux/man-pages/man2/PR_SET_TIMERSLACK.2const.html) (fetched) says init starts with 50,000 ns, children inherit it, it applies to `nanosleep`, `clock_nanosleep`, `poll`, `epoll_wait`, `futex` and similar calls, and "timer slack is not applied to threads that are scheduled under a real-time scheduling policy".
- **An idle CPU wakes slower than a busy one.** With no neighbours the median was higher than with two CPU-bound neighbours. An idle virtual CPU halts, which on a VM is an exit to the hypervisor, and waking it costs tens of microseconds. On bare metal the same effect comes from deep C-states (CO 28). This is my interpretation; the VM's exit counts were not measured here.
- **Two CPU-bound neighbours push the 99th percentile to about 1 ms.** Most wake-ups still preempt the running hog at once (median about 16 µs), because the sleeper has used little CPU and has positive lag. The tail is the wake-ups that had to wait for a tick or for the current task's turn.
- **A 100 µs custom slice did not help here.** That matches section 3: on this kernel, slice decisions wait for the tick.
- **`SCHED_FIFO` removed the tail**: the 99th percentile fell from about 1 ms to tens of microseconds, because a real-time wake-up preempts any normal task immediately.

**Voluntary or involuntary?** The kernel counts why each task left the CPU. A task that blocks (sleeps, waits for I/O or a lock) switches **voluntarily**; a task that is preempted switches **involuntarily**. It is the first number to look at when a thread is slow:

```c
/* l7_switches.c: voluntary and involuntary context switches tell you why a task left the CPU.
   A sleeper (1000 sleeps of 1 ms) and a spinner share CPU 1 for about 2 seconds. */
#include "util.h"
#include <sys/wait.h>

static void report(const char *who)
{
    printf("%-8s voluntary %5ld   involuntary %5ld\n", who,
           status_field("voluntary_ctxt_switches"), status_field("nonvoluntary_ctxt_switches"));
    fflush(stdout);
}

int main(void)
{
    pid_t spinner = fork();
    if (spinner == 0) {
        pin(1);
        double end = now() + 2.2;
        while (now() < end) { }
        usleep(100000);                                  /* print second */
        report("spinner");
        _exit(0);
    }
    if (fork() == 0) {
        pin(1);
        for (int i = 0; i < 1000; i++) usleep(1000);     /* blocks: gives the CPU up itself */
        report("sleeper");
        _exit(0);
    }
    while (wait(NULL) > 0) { }
    return 0;
}
```

```text
sleeper  voluntary  1001   involuntary     0
spinner  voluntary     1   involuntary  1009
```

The sleeper gave up the CPU 1,000 times itself. The spinner was preempted about 1,000 times: once per sleeper wake-up, which confirms that wake-ups preempt. High voluntary counts send you to I/O and locks; high involuntary counts send you to CPU contention and priorities.

---

## 5. Several CPUs: load balancing

With per-CPU run queues, something must keep the queues even. Linux does it in three places (from memory; the [kernel documentation index](https://www.kernel.org/doc/html/latest/scheduler/index.html) has the details under "scheduler domains"):

- **On wake-up**, the waking task is placed on a CPU: preferably an idle one near where it last ran (warm caches), or near its waker.
- **When a CPU goes idle**, it tries to pull work from a busy CPU (newly idle balancing). This is the work stealing of OS 04 section 11.
- **Periodically**, each CPU checks whether its queue is busier than others and migrates tasks. The checks are arranged in a hierarchy of **scheduling domains** (SMT siblings, cores sharing a cache, a NUMA node, the whole machine), balancing often between close CPUs and rarely across NUMA nodes, where a migration costs most (OS 13).

Three CPU-bound processes on two CPUs cannot be split evenly by placement alone: one CPU gets two. Fairness then needs migrations. The program measures what each process received in 6 seconds:

```c
/* l2_balance.c: N CPU-bound processes, not pinned, on a machine with fewer CPUs.
   Does the load balancer give each its fair share?  Usage: l2_balance SECONDS N */
#include "util.h"
#include <sys/wait.h>

int main(int argc, char **argv)
{
    if (argc != 3) return 1;
    double secs = atof(argv[1]);
    int n = atoi(argv[2]), go[2], res[2];
    if (pipe(go) || pipe(res)) return 1;
    for (int i = 0; i < n; i++)
        if (fork() == 0) {
            double end;
            close(go[1]);
            if (read(go[0], &end, sizeof end) != sizeof end) _exit(1);
            while (now() < end) { }
            pid_t me = getpid();
            char line[96];
            int len = snprintf(line, sizeof line, "%.0f %.0f %.0f\n",
                               sched_field(me, "se.sum_exec_runtime"),
                               sched_field(me, "se.nr_migrations"),
                               sched_field(me, "nr_involuntary_switches"));
            if (write(res[1], line, len) != len) _exit(1);
            _exit(0);
        }
    close(go[0]);
    double end = now() + secs;
    for (int i = 0; i < n; i++)
        if (write(go[1], &end, sizeof end) != sizeof end) return 1;
    close(res[1]);
    for (int i = 0; i < n; i++) wait(NULL);

    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    double fair = 1000 * secs * (cpus < n ? cpus : n) / n;
    FILE *f = fdopen(res[0], "r");
    printf("%d processes on %ld CPUs for %.0f s: a fair share is %.0f ms each\n", n, cpus, secs, fair);
    printf("process  CPU ms  of fair  migrations  involuntary switches\n");
    for (int i = 0; i < n; i++) {
        double run, mig, inv;
        if (fscanf(f, "%lf %lf %lf", &run, &mig, &inv) != 3) return 1;
        printf("%7d  %6.0f  %6.1f%%  %10.0f  %20.0f\n", i, run, 100 * run / fair, mig, inv);
    }
    return 0;
}
```

```text
3 processes on 2 CPUs for 6 s: a fair share is 4000 ms each
process  CPU ms  of fair  migrations  involuntary switches
      0    3922    98.0%          16                   533
      1    3828    95.7%          18                   547
      2    4138   103.5%          10                   478
```

```text
3 processes on 2 CPUs for 6 s: a fair share is 4000 ms each
process  CPU ms  of fair  migrations  involuntary switches
      0    3882    97.0%          17                   539
      1    3902    97.5%          16                   539
      2    4143   103.6%          11                   490
```

Each process got close to its fair share of 4,000 ms (two thirds of a CPU), with only tens of migrations in 6 seconds. The balancer does not shuffle tasks every tick; it moves one occasionally so that, averaged over time, the CPU each task spent alone and the CPU it spent sharing even out.

**Affinity** (`sched_setaffinity`, `taskset`) restricts which CPUs a task may use; OS 04 section 11 measured the cost of pinning two threads to one CPU. Pinning makes behaviour predictable and is the basis of CPU isolation (section 10), at the price of disabling the balancer's help.

---

## 6. Groups: cgroups, shares and quotas

Fairness between tasks is not always what you want. If user A runs one process and user B runs a hundred, task-level fairness gives B 99 percent of the CPU. **Group scheduling** applies the fair algorithm at several levels: the CPU is first shared between groups by group weight, then each group's share between its tasks. The [CFS documentation](https://www.kernel.org/doc/html/latest/scheduler/sched-design-CFS.html) gives the example of a `multimedia` group with `cpu.shares` 2048 getting twice the CPU of a `browser` group with 1024. Containers are built on this (LX 02). Two controls matter:

- **Weight**: `cpu.shares` in cgroup v1 (default 1024), `cpu.weight` in cgroup v2 (default 100; from memory). Weights only matter when the CPU is contended.
- **Bandwidth quota**: `cpu.cfs_quota_us` per `cpu.cfs_period_us` in v1, `cpu.max` in v2. A hard cap that applies even when the CPU is otherwise idle: once the group has used its quota in a period it is **throttled** until the next period.

This machine mounts the cgroup v1 `cpu` controller, so the script uses the v1 files (the v2 equivalents are named above). `spin` is a small helper that burns CPU on one CPU and prints the CPU time it received:

```c
/* spin.c: burn CPU on one CPU for SECONDS, then print the CPU time received.
   Usage: spin CPU SECONDS */
#include "util.h"

int main(int argc, char **argv)
{
    if (argc != 3) return 1;
    pin(atoi(argv[1]));
    double end = now() + atof(argv[2]);
    while (now() < end) { }
    printf("%.0f\n", sched_field(getpid(), "se.sum_exec_runtime"));
    return 0;
}
```

```sh
#!/bin/sh
# l6_cgroup.sh: the fair scheduler's group controls (cgroup v1 "cpu" controller, as mounted here).
# Needs root and the spin program from this section.
CG=/sys/fs/cgroup/cpu
for g in A B C; do mkdir -p $CG/os05$g; done
echo 2048 > $CG/os05A/cpu.shares                 # group A: weight 2048
echo 1024 > $CG/os05B/cpu.shares                 # group B: weight 1024 (the default)
echo 100000 > $CG/os05C/cpu.cfs_period_us        # group C: at most 20 ms every 100 ms
echo 20000 > $CG/os05C/cpu.cfs_quota_us

run_in() { sh -c "echo \$\$ > $CG/$1/tasks; exec ./spin 1 4" > /tmp/os05_$1; }

echo "1. Two groups on CPU 1, shares 2048 and 1024, one spinner each, 4 s:"
run_in os05A & run_in os05B & wait
echo "   A got $(cat /tmp/os05_os05A) ms, B got $(cat /tmp/os05_os05B) ms"

echo "2. Group B now holds two spinners and group A one (same shares):"
run_in os05A & run_in os05B & sh -c "echo \$\$ > $CG/os05B/tasks; exec ./spin 1 4" > /tmp/os05_B2 & wait
echo "   A got $(cat /tmp/os05_os05A) ms; B's spinners got $(cat /tmp/os05_os05B) and $(cat /tmp/os05_B2) ms"

echo "3. Group C has a quota of 20 ms per 100 ms; one spinner alone on CPU 1 for 4 s:"
run_in os05C
echo "   C got $(cat /tmp/os05_os05C) ms; throttled periods: $(grep nr_throttled $CG/os05C/cpu.stat | cut -d' ' -f2)"

for g in A B C; do rmdir $CG/os05$g; done
rm -f /tmp/os05_*
```

```text
1. Two groups on CPU 1, shares 2048 and 1024, one spinner each, 4 s:
   A got 2656 ms, B got 1327 ms
2. Group B now holds two spinners and group A one (same shares):
   A got 2647 ms; B's spinners got 666 and 662 ms
3. Group C has a quota of 20 ms per 100 ms; one spinner alone on CPU 1 for 4 s:
   C got 823 ms; throttled periods: 41
```

- With shares 2048 against 1024, group A got two thirds of the CPU and B one third.
- When B ran **two** spinners, A **still** got two thirds; B's third was split between its two tasks. Group weights protect a group from another group's task count.
- The 20 ms per 100 ms quota held the spinner to about 20 percent of the CPU even though nothing else wanted it, and the kernel counted dozens of throttled periods. This is how a container with "0.2 CPUs" behaves, and why a latency-sensitive service in a tightly limited container shows stalls of up to a period: when the quota runs out, every thread in the group waits for the next period.

**Autogroup** (enabled here, `sched_autogroup_enabled` = 1) puts each terminal session in its own group automatically, so a `make -j64` in one terminal does not starve an editor in another (from memory).

---

## 7. The real-time classes, briefly

OS 04 section 9 measured the real-time policies on this machine: priorities 1 to 99, `SCHED_FIFO` runs until it blocks or a higher priority arrives, `SCHED_RR` adds a quantum (100 ms here, `sched_rr_timeslice_ms` in section 1), and **real-time throttling** (`sched_rt_runtime_us` 950,000 per `sched_rt_period_us` 1,000,000) left a normal task about 5 percent of a CPU under a `SCHED_FIFO` hog. Three things to add:

- **Pin real-time threads.** On several CPUs the real-time class pushes and pulls tasks so that the highest-priority runnable real-time tasks are the ones running (from memory). An unpinned real-time thread can still migrate and land next to the wrong work.
- **Real-time priority does not help a thread that blocks on something held by a normal thread.** That is priority inversion (OS 07); use priority-inheritance mutexes (`PTHREAD_PRIO_INHERIT`).
- **`SCHED_IDLE` and `SCHED_BATCH` are the other direction.** `SCHED_IDLE` is "even weaker than nice 19" and `SCHED_BATCH` "does not preempt nearly as often as regular tasks would" (both from the CFS documentation). Problem E uses `SCHED_IDLE` to keep background work out of a control loop's way.

---

## 8. `SCHED_DEADLINE`

`SCHED_DEADLINE` is OS 04's EDF in the kernel, made safe for an open system. According to the [kernel documentation](https://www.kernel.org/doc/html/latest/scheduler/sched-deadline.html):

- A task declares a **runtime** (CPU time it needs), a **deadline** and a **period** (in nanoseconds, through `sched_setattr`; glibc has no wrapper).
- A **constant bandwidth server** gives each task at most its runtime per period; a task that uses up its runtime is **throttled** until its deadline. A buggy deadline task cannot take more than it declared, which is what makes EDF usable among other tasks.
- **Admission control** refuses a task (`EBUSY`) when the total bandwidth of deadline tasks would exceed `M * (sched_rt_runtime_us / sched_rt_period_us)` for `M` CPUs, 2 times 0.95 = 1.9 here.
- A deadline task cannot fork, "in order not to jeopardize the admission control".

The program checks each rule. It first tries to make a task pinned to one CPU a deadline task, then starts four tasks asking for 6 ms every 10 ms (0.6 of a CPU each), plus one normal spinner:

```c
/* l5_deadline.c: SCHED_DEADLINE admission control and runtime enforcement.
   Four CPU-bound children each ask for 6 ms of CPU every 10 ms (60 percent of a CPU).
   One SCHED_OTHER spinner competes. Everything runs for 3 seconds. */
#include "util.h"
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#ifndef SCHED_DEADLINE
#define SCHED_DEADLINE 6
#endif

struct sched_attr {
    uint32_t size, sched_policy;
    uint64_t sched_flags;
    int32_t sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime, sched_deadline, sched_period;
};

static int make_deadline(uint64_t runtime_ns, uint64_t period_ns)
{
    struct sched_attr a = { .size = sizeof a, .sched_policy = SCHED_DEADLINE,
                            .sched_runtime = runtime_ns, .sched_deadline = period_ns,
                            .sched_period = period_ns };
    return syscall(SYS_sched_setattr, 0, &a, 0) == 0 ? 0 : errno;
}

int main(void)
{
    const double secs = 3;
    double end = now() + secs + 0.5;

    /* Check 1: a SCHED_DEADLINE task may not be pinned to a subset of the CPUs. */
    if (fork() == 0) {
        pin(1);
        int e = make_deadline(6000000, 10000000);
        printf("pinned to CPU 1, then SCHED_DEADLINE: %s\n", e ? strerror(e) : "accepted");
        fflush(stdout);
        _exit(0);
    }
    wait(NULL);

    /* Check 2: admission control, and what each accepted task receives. */
    int res[2];
    if (pipe(res)) return 1;
    pid_t kids[5];
    for (int i = 0; i < 5; i++) {
        if ((kids[i] = fork()) == 0) {
            int e = 0, fork_err = 0;
            double start = now();
            if (i < 4) {
                e = make_deadline(6000000, 10000000);
                if (e == 0) {                                  /* may a deadline task fork? */
                    pid_t p = fork();
                    if (p == 0) _exit(0);
                    if (p < 0) fork_err = errno; else waitpid(p, NULL, 0);
                }
            }
            if (e == 0) while (now() < end) { }                /* burn CPU until the end */
            pid_t me = getpid();
            char line[160];
            int len = snprintf(line, sizeof line, "%d %d %d %.0f %.0f\n", i, e, fork_err,
                               e ? 0.0 : sched_field(me, "se.sum_exec_runtime"),
                               1000 * (end - start));
            if (write(res[1], line, len) != len) _exit(1);
            _exit(0);
        }
        usleep(100000);                                        /* let it be admitted first */
    }
    close(res[1]);
    while (wait(NULL) > 0) { }
    FILE *f = fdopen(res[0], "r");
    char out[5][200];
    int i, e, fe;
    double ms, window;
    while (fscanf(f, "%d %d %d %lf %lf", &i, &e, &fe, &ms, &window) == 5) {
        if (i == 4)
            snprintf(out[i], 200, "child 4: SCHED_OTHER spinner, %.0f ms of CPU in %.0f ms (%.0f%% of a CPU)",
                     ms, window, 100 * ms / window);
        else if (e)
            snprintf(out[i], 200, "child %d: SCHED_DEADLINE 6 ms / 10 ms refused: %s", i, strerror(e));
        else
            snprintf(out[i], 200, "child %d: SCHED_DEADLINE 6 ms / 10 ms accepted, %.0f ms of CPU in %.0f ms"
                     " (%.0f%%); fork() %s", i, ms, window, 100 * ms / window, fe ? strerror(fe) : "succeeded");
    }
    for (i = 0; i < 5; i++) puts(out[i]);
    return 0;
}
```

```text
pinned to CPU 1, then SCHED_DEADLINE: Operation not permitted
child 0: SCHED_DEADLINE 6 ms / 10 ms accepted, 2101 ms of CPU in 3499 ms (60%); fork() Resource temporarily unavailable
child 1: SCHED_DEADLINE 6 ms / 10 ms accepted, 2040 ms of CPU in 3399 ms (60%); fork() Resource temporarily unavailable
child 2: SCHED_DEADLINE 6 ms / 10 ms accepted, 1979 ms of CPU in 3294 ms (60%); fork() Resource temporarily unavailable
child 3: SCHED_DEADLINE 6 ms / 10 ms refused: Device or resource busy
child 4: SCHED_OTHER spinner, 279 ms of CPU in 3074 ms (9% of a CPU)
```

- Three requests (1.8 CPUs) were accepted; the fourth would have made 2.4, above 1.9, and was refused with `EBUSY`.
- Each accepted task received **exactly 60 percent** of a CPU over its run, even though it wanted to spin all the time: the server throttled it after 6 ms in every 10.
- `fork()` from a deadline task failed with `EAGAIN`.
- A task restricted to one CPU was refused with `EPERM`. The man page lists this error ("the thread's CPU affinity mask does not include all CPUs"): admission control reasons about the whole set of CPUs, so a deadline task may not be pinned to part of it.
- **Unexplained:** the normal spinner received about 10 percent of a CPU, while 0.2 CPUs were left over after the three deadline tasks. I did not find where the rest went (the parent process sleeps, and nothing else ran). Treat the 10 percent as measured and the reason as open.

---

## 9. `sched_ext`: schedulers written in BPF

According to [kernelnewbies](https://kernelnewbies.org/Linux_6.12), Linux 6.12 "includes the first pieces of `sched_ext`". It is a scheduling class whose policy is a BPF program loaded from user space. From the [kernel documentation](https://www.kernel.org/doc/html/latest/scheduler/sched-ext.html):

- The BPF program fills in `struct sched_ext_ops` callbacks: `select_cpu()` (a hint where a waking task should go), `enqueue()` (where a runnable task is queued) and `dispatch()` (called when a CPU has nothing to run).
- Tasks wait in **dispatch queues** (DSQs): one global, one local per CPU, plus any the program creates. "A CPU always executes a task from its local DSQ."
- Safety: if the program errors out, stalls a runnable task, or the user presses SysRq-S, the kernel "reverts all tasks back to the fair-class scheduler". A bad BPF scheduler cannot hang the machine for long.
- Examples ship in `tools/sched_ext/`: `scx_simple` (one global FIFO), `scx_qmap` (five FIFO levels), `scx_central` (one CPU decides for all).

This kernel was built without `CONFIG_SCHED_CLASS_EXT` (section 1), so nothing here was run. The interview point: since 6.12, trying a new policy on production machines no longer requires a custom kernel.

---

## 10. Preemption models, PREEMPT_RT and CPU isolation

### 10.1 When can the kernel itself be preempted?

User-space code can always be preempted. The question is what happens while a task is **inside the kernel** (in a system call or a fault handler) when a more important task becomes runnable. That is a build and boot choice:

| Model | Kernel code is preempted | Good for |
|---|---|---|
| `PREEMPT_NONE` | only where it blocks or returns to user space | throughput (servers) |
| `PREEMPT_VOLUNTARY` | also at explicit `cond_resched()` points | desktops (old default) |
| `PREEMPT` (full) | anywhere outside critical sections (spinlocks held, interrupts off) | low latency |
| `PREEMPT_LAZY` | normal tasks wait for the next tick; real-time wake-ups preempt at once | intended to replace none and voluntary |
| `PREEMPT_RT` | almost everywhere: most spinlocks become sleeping locks, interrupt handlers run in threads | bounded worst-case latency |

- **This machine** was built with `PREEMPT_DYNAMIC` (the model is chosen at boot with `preempt=`) and booted as "Dynamic Preempt: none".
- **PREEMPT_RT** was merged in Linux 6.12 (kernelnewbies: "the real-time patchset has been merged"). It trades some throughput for latency bounded at tens of microseconds (from memory).
- **Lazy preemption** was merged for 6.13. According to [LWN](https://lwn.net/Articles/994322/), most reschedule requests set a new "lazy" flag that is acted on at the next return to user space or converted at the next timer tick, while a real-time task becoming runnable still causes "an almost immediate preemption". The stated goal is to keep only lazy and full, and to remove most `cond_resched()` calls.
- **Reported, not verified here:** a [news write-up](https://lilting.ch/en/articles/linux-7-preempt-none-postgresql-throughput-halved) says Linux 7.0 removed `PREEMPT_NONE` and `PREEMPT_VOLUNTARY` on the major architectures. It also reports a PostgreSQL benchmark falling to about half its throughput, because a preempted holder of a user-space spinlock leaves other processes spinning. I did not check the kernel tree or the mailing-list thread. The mechanism is worth knowing for any user-space spinlock (OS 06).

### 10.2 Where latency goes, and isolating a CPU

When a pinned thread still sees spikes, the usual suspects are (from memory, a standard list):

1. preemption by a higher-priority task, or a fair-class task that is owed time;
2. interrupts and softirqs on that CPU, network receive in particular (DRV 05, NET 06);
3. the timer tick itself;
4. kernel work on that CPU: RCU callbacks, kworker threads, page reclaim;
5. TLB-shootdown interrupts from another thread's `munmap` or `mprotect` (OS 12);
6. memory compaction for transparent huge pages (OS 12);
7. waking from an idle state (measured in section 4: an idle virtual CPU woke tens of microseconds later than a busy one).

Isolating a CPU for one thread takes several settings together (boot parameters, from memory): `isolcpus=` keeps the scheduler from placing other tasks there; `nohz_full=` stops the tick when exactly one task is runnable (needs `CONFIG_NO_HZ_FULL`, absent here); `rcu_nocbs=` moves RCU callbacks elsewhere; `irqaffinity=` routes device interrupts to the other CPUs; and the thread is pinned and usually made `SCHED_FIFO`. Naming only `isolcpus` is the common incomplete answer. `nohz_full` does nothing when a second task is runnable on that CPU.

**Measuring.** `perf sched record` and `perf sched latency` report per-task scheduling delay; `cyclictest` measures the worst-case wake-up latency of a whole system and is the standard tool on real-time kernels; `/proc/<pid>/sched` and `/proc/<pid>/status` give per-task counters, as used throughout this chapter. `perf` was not installed on this machine.

---

## What it costs

| Mechanism | Measured effect here | Section |
|---|---|---|
| Nice -5 against 0 against 5 | 69.7 / 22.8 / 7.5 percent of a CPU | 2.2 |
| Effective slice for CPU-bound tasks | about 4 ms (one tick), whatever slice was requested | 3 |
| Timer slack for a normal task | adds about 50 µs to every timed wake-up | 4 |
| Two CPU-bound neighbours on a periodic task's CPU | 99th-percentile lateness about 1 ms | 4 |
| Same with `SCHED_FIFO` | 99th percentile tens of µs | 4 |
| Three tasks on two CPUs | each within a few percent of its fair share | 5 |
| cgroup quota 20 ms / 100 ms | 20 percent of a CPU even with the CPU otherwise idle | 6 |
| `SCHED_DEADLINE` 6 ms / 10 ms | exactly 60 percent of a CPU; the fourth such task refused | 8 |

---

## Common mistakes

1. **Saying "CFS" for a current kernel.** The fair class has used EEVDF since 6.6; say "CFS, replaced by EEVDF in 6.6".
2. **Treating nice as a priority.** It is a weight; nice 19 still runs, at 15/1024 of a nice-0 task's rate.
3. **Expecting the slice to be enforced exactly.** Here every slice was rounded to the 4 ms tick.
4. **Asking for a long slice to avoid preemption.** Under EEVDF a long slice means a late deadline; the task is preempted at least as often.
5. **Measuring a VM's scheduling from gaps a program sees.** The hypervisor's pauses look the same; use the kernel's counters.
6. **Forgetting timer slack.** Every `nanosleep` of a normal thread may be 50 µs late on purpose.
7. **Assuming an idle CPU gives the best wake-up latency.** Waking an idle CPU costs more than preempting a busy one.
8. **Using `SCHED_FIFO` without pinning and without a priority-inheritance mutex.**
9. **Disabling RT throttling (`sched_rt_runtime_us = -1`) without a watchdog.** One runaway loop then owns the CPU.
10. **Confusing cgroup weight and quota.** Weight shares a contended CPU; quota caps the group even when the CPU is idle, and causes stalls of up to a period.
11. **Pinning a `SCHED_DEADLINE` task.** The call fails with `EPERM`.
12. **Naming `isolcpus` alone for CPU isolation.** The tick, RCU callbacks and interrupts still land there.
13. **Calling `sched_setattr` through glibc.** There is no wrapper; use `syscall(SYS_sched_setattr, ...)` with your own `struct sched_attr`.
14. **Printing in a child and leaving with `_exit` without `fflush`.** The first version of the slice program printed nothing for that reason (OS 02).

---

## Questions out loud

1. **How does Linux pick the next task?** Highest class first (stop, deadline, real-time, fair, ext, idle); within the fair class, EEVDF: earliest virtual deadline among eligible tasks.
2. **What was CFS?** Weighted fair queuing: each task's `vruntime` grows by `t * 1024 / weight`; run the smallest `vruntime`, kept in an rbtree.
3. **Why EEVDF?** CFS had no way to express latency needs separately from share. EEVDF adds a virtual deadline from the requested slice, so short-slice tasks run sooner without getting more CPU.
4. **Define lag and eligibility.** `lag_i = w_i (V - v_i)` with `V` the weighted average `vruntime`; eligible when lag is at least zero.
5. **How does a nice value become a share?** Through the weight table (1.25 times per step); shares are proportional to weights (69.7 / 22.9 / 7.5 percent predicted for nice -5, 0, 5; measured within 0.1 point).
6. **What is the default time slice?** 0.7 ms times `(1 + ilog2(ncpus))`; but a CPU-bound task is only preempted at a tick or a wake-up, so here it ran 4 ms at a time.
7. **Why can a sleeping thread wake up late on an idle machine?** Timer slack (50 µs), and an idle CPU's wake-up cost.
8. **Your control loop shares a CPU with batch jobs and is 1 ms late at the 99th percentile. Fixes?** `SCHED_FIFO` or nice -20 for the loop, `SCHED_IDLE` for the batch jobs, or move the batch jobs (Problem E measures all of them).
9. **How does Linux balance load?** Placement on wake-up, pulling when a CPU goes idle, and periodic balancing across a hierarchy of scheduling domains.
10. **What do `cpu.shares` and `cpu.cfs_quota_us` do?** Weight among groups when contended; a hard cap per period, which throttles.
11. **What stops a `SCHED_FIFO` loop from hanging the machine?** RT throttling: 950 ms per second, leaving 5 percent.
12. **What is `SCHED_DEADLINE`?** EDF plus a constant-bandwidth server; runtime, deadline and period; admission control up to 95 percent per CPU; no fork; cannot be pinned.
13. **What is `sched_ext`?** A class whose policy is a BPF program (6.12+), with automatic fallback to the fair class on error.
14. **Preemption models?** None, voluntary, full, lazy, RT; what each allows inside the kernel; lazy replaces the first two.
15. **How do you isolate a CPU for a latency-critical thread?** `isolcpus`, `nohz_full`, `rcu_nocbs`, `irqaffinity`, pinning and a real-time policy, then measure with `cyclictest`.
16. **Voluntary against involuntary switches?** Blocking against preemption; they point to different investigations.

---

## Problems

**Where to solve them.** On any Linux machine with a recent kernel (6.6 or later for EEVDF, 6.12 or later for custom slices). A, B and D need no special set-up; A, D and E need root for the negative nice value, `SCHED_DEADLINE` and `SCHED_FIFO`; C needs root and the cgroup `cpu` controller (on a cgroup v2 system, translate `cpu.shares` to `cpu.weight` and the quota files to `cpu.max`). A VM with at least 2 CPUs is enough. Numbers will differ on your machine; the ratios should not.

**How to approach them.** Predict on paper first: weights for A, `V`, lags and deadlines for B, group then task shares for C, cumulative bandwidth for D. For E, decide which of the five fixes changes whom the scheduler picks at wake-up, then measure.

### Problem A (E, entry level): predict the shares

Three CPU-bound processes share one CPU with nice 0, 1 and 5. What percentage of the CPU does each receive? Check with `l1_weights 4 0 1 5`.

### Problem B (E, entry level): one EEVDF decision

Tasks A and B (nice 0) have `vruntime` 10 and 12 ms; C (nice 5) has 11 ms; all request the 1.4 ms default slice. Compute `V`, each lag, which are eligible, each virtual deadline, and which runs. Then repeat with A requesting 20 ms and C 0.2 ms.

### Problem C (M, medium): groups and quotas

On one CPU, group A (shares 2048) has one spinner and group B (shares 1024) has three. How much does each of the four spinners get? Then a group with quota 50 ms per 100 ms runs one spinner on each of two CPUs: how much does each get?

### Problem D (M, medium): admission control

On this 2-CPU machine (limit 1.9), these `SCHED_DEADLINE` requests arrive in order and all stay alive (runtime, deadline, period in ms): (10, 30, 30), (45, 50, 50), (20, 40, 40), (5, 10, 10), (1, 20, 20), (8, 5, 10). Which are accepted?

### Problem E (H, hard): fix a late control loop

A loop that wakes every 1 ms shares CPU 1 with two CPU-bound jobs. Its 99th-percentile lateness is about 1 ms. Measure five options: no change, nice -20 for the loop, `SCHED_IDLE` for the jobs, `SCHED_FIFO` for the loop, and moving the jobs to CPU 0. Explain each result.

---

## Answers

**A.** Weights 1024, 820 and 335, sum 2179: 47.0, 37.6 and 15.4 percent.

```text
nice  weight  load.weight  CPU ms  share   predicted  vruntime ms
   1     820       839680    1494   37.6%      37.6%       3919.5
   0    1024      1048576    1866   47.0%      47.0%       3920.2
   5     335       343040     613   15.4%      15.4%       3926.9
```

**B.** The program applies the three formulas of section 2.3:

```c
/* pb.c: one EEVDF decision by hand, as fair.c describes it.
   V = weighted average of the vruntimes; lag_i = w_i * (V - v_i); eligible if lag_i >= 0;
   virtual deadline vd_i = v_i + slice_i * 1024 / w_i; run the eligible task with the earliest vd. */
#include <stdio.h>

struct task { const char *name; int weight; double vruntime_ms, slice_ms; };

static void decide(struct task *t, int n)
{
    double sw = 0, swv = 0;
    for (int i = 0; i < n; i++) { sw += t[i].weight; swv += t[i].weight * t[i].vruntime_ms; }
    double V = swv / sw;
    int pick = -1;
    double best = 1e18;
    printf("V = %.3f ms\n", V);
    for (int i = 0; i < n; i++) {
        double lag = t[i].weight * (V - t[i].vruntime_ms) / 1024;   /* in nice-0 milliseconds */
        double vd = t[i].vruntime_ms + t[i].slice_ms * 1024 / t[i].weight;
        int ok = t[i].vruntime_ms <= V;
        printf("  %s: weight %4d  v %6.3f  lag %+7.3f  %-10s  deadline %7.3f\n", t[i].name,
               t[i].weight, t[i].vruntime_ms, lag, ok ? "eligible" : "ineligible", vd);
        if (ok && vd < best) { best = vd; pick = i; }
    }
    printf("  runs: %s\n", t[pick].name);
}

int main(void)
{
    struct task one[] = { {"A (nice 0)", 1024, 10.0, 1.4}, {"B (nice 0)", 1024, 12.0, 1.4},
                          {"C (nice 5)",  335, 11.0, 1.4} };
    puts("Case 1: default slices");
    decide(one, 3);
    struct task two[] = { {"A (nice 0, 20 ms slice)", 1024, 10.0, 20.0}, {"B (nice 0)", 1024, 12.0, 1.4},
                          {"C (nice 5, 0.2 ms slice)", 335, 11.0, 0.2} };
    puts("Case 2: A asks for a 20 ms slice, C for 0.2 ms");
    decide(two, 3);
    return 0;
}
```

```text
Case 1: default slices
V = 11.000 ms
  A (nice 0): weight 1024  v 10.000  lag  +1.000  eligible    deadline  11.400
  B (nice 0): weight 1024  v 12.000  lag  -1.000  ineligible  deadline  13.400
  C (nice 5): weight  335  v 11.000  lag  +0.000  eligible    deadline  15.279
  runs: A (nice 0)
Case 2: A asks for a 20 ms slice, C for 0.2 ms
V = 11.000 ms
  A (nice 0, 20 ms slice): weight 1024  v 10.000  lag  +1.000  eligible    deadline  30.000
  B (nice 0): weight 1024  v 12.000  lag  -1.000  ineligible  deadline  13.400
  C (nice 5, 0.2 ms slice): weight  335  v 11.000  lag  +0.000  eligible    deadline  11.611
  runs: C (nice 5, 0.2 ms slice)
```

The lags sum to zero (+1, -1, 0), as they must. In case 2, A's long slice pushes its deadline to 30 and C's short slice brings its deadline to 11.61, so C runs although it has the lowest weight: a short slice buys promptness, not share.

**C.** Part 1: group shares first, A 2/3, B 1/3; B's third splits three ways, 1/9 each: about 2,667 ms and 444 ms in 4 s. Part 2: the quota is for the whole group across all CPUs, 0.5 of a CPU in total, so each spinner gets about a quarter of a CPU, about 1,000 ms in 4 s.

```sh
#!/bin/sh
# pc.sh: Problem C. Needs root, cgroup v1 "cpu" mounted at /sys/fs/cgroup/cpu, and ./spin.
CG=/sys/fs/cgroup/cpu
mkdir -p $CG/pcA $CG/pcB $CG/pcQ
echo 2048 > $CG/pcA/cpu.shares
echo 1024 > $CG/pcB/cpu.shares
echo 100000 > $CG/pcQ/cpu.cfs_period_us
echo 50000 > $CG/pcQ/cpu.cfs_quota_us
run() { sh -c "echo \$\$ > $CG/$1/tasks; exec ./spin $2 4" > /tmp/pc_$3; }

echo "Part 1: on CPU 1, group A (2048) has one spinner, group B (1024) has three; 4 s"
run pcA 1 a & run pcB 1 b1 & run pcB 1 b2 & run pcB 1 b3 & wait
echo "  A: $(cat /tmp/pc_a) ms   B: $(cat /tmp/pc_b1), $(cat /tmp/pc_b2), $(cat /tmp/pc_b3) ms"

echo "Part 2: group Q has 50 ms per 100 ms; one spinner on CPU 0 and one on CPU 1; 4 s"
run pcQ 0 q0 & run pcQ 1 q1 & wait
echo "  Q: $(cat /tmp/pc_q0) ms and $(cat /tmp/pc_q1) ms"

rmdir $CG/pcA $CG/pcB $CG/pcQ
rm -f /tmp/pc_*
```

```text
Part 1: on CPU 1, group A (2048) has one spinner, group B (1024) has three; 4 s
  A: 2646 ms   B: 447, 443, 460 ms
Part 2: group Q has 50 ms per 100 ms; one spinner on CPU 0 and one on CPU 1; 4 s
  Q: 1038 ms and 1020 ms
```

**D.** The running total of runtime/period: 0.333, 1.233, 1.733 (accepted), then 2.233 > 1.9 (refused), then 1.783 (accepted: the refused task does not count). The last one has runtime 8 ms greater than its 5 ms deadline, which can never be met, and is rejected as invalid (`EINVAL`) before admission control.

```c
/* pd.c: which SCHED_DEADLINE requests does the kernel admit, in this order?
   Each accepted task sleeps (it uses no CPU) and stays alive until the end, so its
   bandwidth stays reserved while the next request is made. */
#include "util.h"
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#ifndef SCHED_DEADLINE
#define SCHED_DEADLINE 6
#endif

struct sched_attr {
    uint32_t size, sched_policy;
    uint64_t sched_flags;
    int32_t sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime, sched_deadline, sched_period;
};

int main(void)
{
    const int req[][3] = {          /* runtime, deadline, period in ms */
        {10, 30, 30}, {45, 50, 50}, {20, 40, 40}, {5, 10, 10}, {1, 20, 20}, {8, 5, 10} };
    const int n = sizeof req / sizeof req[0];
    pid_t kids[8];
    double total = 0;
    for (int i = 0; i < n; i++) {
        int p[2];
        if (pipe(p)) return 1;
        if ((kids[i] = fork()) == 0) {
            struct sched_attr a = { .size = sizeof a, .sched_policy = SCHED_DEADLINE,
                                    .sched_runtime = req[i][0] * 1000000ULL,
                                    .sched_deadline = req[i][1] * 1000000ULL,
                                    .sched_period = req[i][2] * 1000000ULL };
            int e = syscall(SYS_sched_setattr, 0, &a, 0) == 0 ? 0 : errno;
            if (write(p[1], &e, sizeof e) != sizeof e) _exit(1);
            pause();                                     /* hold the reservation */
            _exit(0);
        }
        int e;
        if (read(p[0], &e, sizeof e) != sizeof e) return 1;
        double u = (double)req[i][0] / req[i][2];
        if (!e) total += u;
        printf("runtime %2d, deadline %2d, period %2d ms (U %.3f): %-34s total admitted U %.3f\n",
               req[i][0], req[i][1], req[i][2], u, e ? strerror(e) : "accepted", total);
        close(p[0]);
        close(p[1]);
    }
    for (int i = 0; i < n; i++) kill(kids[i], SIGKILL);
    while (wait(NULL) > 0) { }
    return 0;
}
```

```text
runtime 10, deadline 30, period 30 ms (U 0.333): accepted                           total admitted U 0.333
runtime 45, deadline 50, period 50 ms (U 0.900): accepted                           total admitted U 1.233
runtime 20, deadline 40, period 40 ms (U 0.500): accepted                           total admitted U 1.733
runtime  5, deadline 10, period 10 ms (U 0.500): Device or resource busy            total admitted U 1.733
runtime  1, deadline 20, period 20 ms (U 0.050): accepted                           total admitted U 1.783
runtime  8, deadline  5, period 10 ms (U 0.800): Invalid argument                   total admitted U 1.783
```

**E.** The program:

```c
/* pe.c: a 1 ms periodic task on CPU 1 shares the CPU with two CPU-bound jobs.
   Try five fixes for its wake-up lateness. Usage: pe FIX
   FIX: none | nice | idlehogs | fifo | movehogs */
#include "util.h"
#include <signal.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>

static int cmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    const char *fix = argv[1];
    pid_t kids[2];
    for (int i = 0; i < 2; i++)
        if ((kids[i] = fork()) == 0) {
            pin(strcmp(fix, "movehogs") == 0 ? 0 : 1);
            if (strcmp(fix, "idlehogs") == 0) {          /* hogs become SCHED_IDLE */
                struct sched_param sp = { 0 };
                sched_setscheduler(0, SCHED_IDLE, &sp);
            }
            for (;;) { }
        }
    pin(1);
    prctl(PR_SET_TIMERSLACK, 1UL, 0, 0, 0);
    if (strcmp(fix, "nice") == 0) setpriority(PRIO_PROCESS, 0, -20);
    if (strcmp(fix, "fifo") == 0) {
        struct sched_param sp = { .sched_priority = 50 };
        if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) { perror("SCHED_FIFO"); return 1; }
    }
    enum { N = 5000 };
    static double late[N];
    struct timespec t, w;
    clock_gettime(CLOCK_MONOTONIC, &t);
    for (int i = 0; i < N; i++) {
        t.tv_nsec += 1000000;
        if (t.tv_nsec >= 1000000000) { t.tv_nsec -= 1000000000; t.tv_sec++; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL);
        clock_gettime(CLOCK_MONOTONIC, &w);
        late[i] = (w.tv_sec - t.tv_sec) * 1e6 + (w.tv_nsec - t.tv_nsec) / 1e3;
        if (late[i] > 1000) t = w;
    }
    for (int i = 0; i < 2; i++) kill(kids[i], SIGKILL);
    while (wait(NULL) > 0) { }
    qsort(late, N, sizeof late[0], cmp);
    printf("fix %-8s: lateness us: median %6.1f  p99 %7.1f  p99.9 %7.1f  max %7.1f\n",
           fix, late[N / 2], late[N * 99 / 100], late[N * 999 / 1000], late[N - 1]);
    return 0;
}
```

Run 1:

```text
fix none    : lateness us: median   16.0  p99   979.2  p99.9  1173.8  max  7996.3
fix nice    : lateness us: median   15.6  p99    49.4  p99.9   131.8  max  3975.6
fix idlehogs: lateness us: median   17.2  p99    54.7  p99.9   306.0  max  2847.5
fix fifo    : lateness us: median   17.3  p99    46.0  p99.9    58.2  max  2388.0
fix movehogs: lateness us: median   32.1  p99   111.6  p99.9   269.7  max   985.1
```

Run 2:

```text
fix none    : lateness us: median   16.2  p99   986.1  p99.9  6357.2  max  8006.0
fix nice    : lateness us: median   17.5  p99    42.5  p99.9   388.1  max  5989.4
fix idlehogs: lateness us: median   17.5  p99    48.6  p99.9   584.4  max  2034.5
fix fifo    : lateness us: median   17.1  p99    29.6  p99.9  1988.3  max  2034.4
fix movehogs: lateness us: median   29.9  p99    96.5  p99.9   594.5  max  2493.9
```

- **none**: about 1 ms at the 99th percentile, as in section 4.
- **nice -20**: the tail almost disappears. The loop's weight (88,761) is 87 times a hog's, so its `vruntime` barely advances when it runs and it is always eligible with the earliest deadline when it wakes.
- **`SCHED_IDLE` jobs**: about as good. The jobs now carry the minimum weight, which has the same effect from the other side, and nothing needs privileges for the loop.
- **`SCHED_FIFO`**: tens of microseconds, by class priority; needs privileges and the cautions of section 7.
- **move the jobs**: good tail, but a higher median, because the loop's CPU is now idle between wake-ups and pays the idle wake-up cost of section 4.

The choice in practice: separate the work (other CPUs, or `SCHED_IDLE` for batch) before reaching for real-time priorities.

---

## Cheat sheet

| Item | Value or rule |
|---|---|
| Class order | stop > deadline > real-time > fair > ext > idle |
| Fair class | EEVDF since 6.6; CFS from 2.6.23 |
| `vruntime` | grows by `t * 1024 / weight` |
| EEVDF | `V` = weighted mean `v`; lag `w(V - v)`; eligible if `v <= V`; `vd = v + slice * 1024 / w`; run eligible task with earliest `vd` |
| Weights | nice 0 = 1024; times 1.25 per step; nice -20 = 88761, nice 19 = 15 |
| Base slice | 0.7 ms times `(1 + ilog2(ncpus))`; 1.4 ms here; enforced at the 4 ms tick here |
| Timer slack | 50 µs for normal threads; `PR_SET_TIMERSLACK` |
| RT throttling | 950,000 µs per 1,000,000 µs |
| `SCHED_DEADLINE` | runtime / deadline / period; admission total up to 0.95 times CPUs; no fork; no pinning |
| cgroups | weight: `cpu.shares` (v1) / `cpu.weight` (v2); cap: `cpu.cfs_quota_us` (v1) / `cpu.max` (v2) |
| Isolation | `isolcpus`, `nohz_full`, `rcu_nocbs`, `irqaffinity`, pin, `SCHED_FIFO`; measure with `cyclictest` |
| Preemption models | none, voluntary, full, lazy (6.13), RT (6.12) |

---

## Sources

- [sched(7), Linux man pages](https://man7.org/linux/man-pages/man7/sched.7.html): policies, priorities, RT throttling (fetched for OS 04).
- [sched_setattr(2)](https://man7.org/linux/man-pages/man2/sched_setattr.2.html): `struct sched_attr`, flags, errors including `EBUSY` and `EPERM`, no glibc wrapper (fetched).
- [EEVDF scheduler, kernel documentation](https://www.kernel.org/doc/html/latest/scheduler/sched-eevdf.html): lag, eligibility, virtual deadline, deferred dequeue (fetched).
- [CFS, kernel documentation](https://www.kernel.org/doc/html/latest/scheduler/sched-design-CFS.html): the ideal CPU, `vruntime`, the rbtree, `SCHED_BATCH`, `SCHED_IDLE`, group scheduling (fetched).
- [SCHED_DEADLINE, kernel documentation](https://www.kernel.org/doc/html/latest/scheduler/sched-deadline.html): CBS, admission control, no fork (fetched).
- [sched_ext, kernel documentation](https://www.kernel.org/doc/html/latest/scheduler/sched-ext.html): DSQs, callbacks, fallback (fetched).
- `kernel/sched/core.c` and `kernel/sched/fair.c` in [the Linux source](https://github.com/torvalds/linux/tree/master/kernel/sched): the weight table, the base slice, `avg_vruntime`, eligibility and `update_deadline` (fetched).
- [LWN: An EEVDF CPU scheduler for Linux](https://lwn.net/Articles/925371) and [LWN: Completing the EEVDF scheduler](https://lwn.net/Articles/969062): eligibility, deadlines, deferred dequeue, custom slices (fetched).
- [LWN: lazy preemption](https://lwn.net/Articles/994322/) (fetched); [Kernelnewbies: Linux 6.12](https://kernelnewbies.org/Linux_6.12): `sched_ext`, PREEMPT_RT (fetched).
- [lilting.ch report on Linux 7.0 and PREEMPT_NONE](https://lilting.ch/en/articles/linux-7-preempt-none-postgresql-throughput-halved): secondary source, not verified against the kernel tree.
- [PR_SET_TIMERSLACK(2const)](https://man7.org/linux/man-pages/man2/PR_SET_TIMERSLACK.2const.html): default, affected calls, no slack for real-time threads (fetched).
- Marked "from memory" in the text: the class order, the 10-bit weight scaling, the high-resolution tick feature, the load-balancing structure and scheduling domains, RT push and pull, cgroup v2 defaults, autogroup's purpose, PREEMPT_RT latency figures, and the list of latency sources and isolation parameters.
