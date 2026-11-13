# OS 05 — Process scheduling and latency

> Prerequisite: `04_computer_organization/11_superscalar_and_out_of_order_execution.md` and `04_computer_organization/12_inside_a_modern_core.md`.
> This tutorial is the backbone of every "explain this tail latency" question.

## What the scheduler is optimising

The default Linux scheduler optimises **fairness and throughput**, not latency.
Understanding that one sentence resolves most confusion: when your latency-
sensitive thread gets delayed, the scheduler is usually doing exactly what it was
designed to do.

Latency-sensitive work therefore requires you to *opt out* of the default policy,
which is what most of this tutorial is about.

## CFS, and its successor EEVDF

**CFS** (Completely Fair Scheduler) tracked `vruntime` per task — virtual runtime
weighted by nice value — and always ran the task with the smallest `vruntime`.
Fair, simple, and with no notion of deadline or latency requirement.

**EEVDF** (Earliest Eligible Virtual Deadline First) replaced CFS as the default
in kernel 6.6. It keeps the fairness accounting but adds a *virtual deadline* per
task, derived from a requested time slice. Tasks that need to run briefly and
promptly get shorter slices and therefore earlier deadlines, which lets latency-
sensitive tasks preempt throughput-oriented ones without abandoning fairness. It
also made `sched_setattr`'s `sched_runtime` hint meaningful for normal tasks.

The practical point for an interview: if you say "CFS" for a modern kernel,
someone may correct you. Say "CFS, or EEVDF on 6.6 and later" and you are
demonstrably current.

Key knobs:

```bash
# Minimum granularity and wakeup preemption behaviour (names vary by version):
sysctl kernel.sched_min_granularity_ns
sysctl kernel.sched_wakeup_granularity_ns

# Per-task:
chrt -p <pid>            # show policy and priority
taskset -pc 2 <pid>      # pin to CPU 2
nice / renice            # weight within the fair class
```

## Scheduling classes, in priority order

```
  STOP        (kernel internal, highest)
  DEADLINE    SCHED_DEADLINE — runtime/period/deadline, admission-controlled
  RT          SCHED_FIFO, SCHED_RR — priorities 1-99
  CFS/EEVDF   SCHED_OTHER, SCHED_BATCH, SCHED_IDLE — everything normal
  IDLE        (lowest)
```

```cpp
#include <pthread.h>
#include <sched.h>

// Real-time FIFO. Runs until it blocks or yields; only a higher-priority RT
// task preempts it. Powerful and dangerous: a spinning FIFO thread at priority
// 99 on a core with no other runnable task can wedge that core entirely.
bool make_realtime(std::thread& t, int priority /* 1..99 */) {
    sched_param sp{};
    sp.sched_priority = priority;
    return pthread_setschedparam(t.native_handle(), SCHED_FIFO, &sp) == 0;
}

// Pin to a specific core. Always pair RT priority with pinning — an unpinned
// RT thread can migrate and cause surprising interference.
bool pin_to_core(std::thread& t, int cpu) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return pthread_setaffinity_np(t.native_handle(), sizeof(set), &set) == 0;
}
```

Safety valve you should know about: `kernel.sched_rt_runtime_us` (default
950000 of 1000000) caps RT tasks at 95% of each period so a runaway RT thread
cannot lock you out of the machine. Production RT deployments often disable it —
and then need to be right.

**SCHED_DEADLINE** is the one most candidates have never used. You declare
runtime, deadline and period; the kernel runs admission control and *refuses* the
thread if the set is not schedulable. For periodic work with a hard budget —
which is a lot of embedded and RT audio/network processing — it is the correct
tool and mentioning it signals depth.

## Preemption models

```
CONFIG_PREEMPT_NONE       server: no involuntary preemption in kernel; best throughput
CONFIG_PREEMPT_VOLUNTARY  desktop: explicit reschedule points
CONFIG_PREEMPT            low latency: kernel preemptible except in critical sections
CONFIG_PREEMPT_RT         fully preemptible: most spinlocks become sleeping mutexes,
                          IRQ handlers become threads, priority inheritance everywhere
```

PREEMPT_RT is now largely merged mainline. It trades throughput for bounded
worst-case latency — typically tens of microseconds rather than milliseconds.
The trade is real: RT kernels are measurably slower on throughput benchmarks.
"Which do you want, and why" is a legitimate design discussion, not a free win.

## Where the latency actually goes

This is the section that answers the mock interview scenario. When a pinned
thread stalls, the suspects are:

1. **Involuntary preemption** by a higher-priority or fair-share-owed task.
2. **Interrupts and softirqs** landing on your core. Network RX in particular.
3. **The timer tick** — 100-1000 Hz of periodic interruption unless you disable it.
4. **Kernel work on your behalf**: RCU callbacks, kworker threads, page reclaim.
5. **TLB shootdown IPIs** from another thread's `munmap` (`12_virtual_memory_and_tlb.md`).
6. **THP compaction stalls** (`12_virtual_memory_and_tlb.md`).
7. **Frequency/C-state transitions** — waking from a deep C-state costs
   microseconds, and that is real tail latency on an idle-then-burst workload.

Isolating a core properly means addressing several of these at once:

```bash
# Boot parameters — the real fix, not a runtime tweak:
isolcpus=2,3            # keep the fair scheduler off these cores
nohz_full=2,3           # stop the periodic tick when one task is runnable
rcu_nocbs=2,3           # move RCU callback processing off these cores
irqaffinity=0,1         # route device interrupts to the housekeeping cores

# Runtime:
echo 0 > /sys/devices/system/cpu/cpu2/cpuidle/state3/disable   # block deep C-states
cpupower frequency-set -g performance
```

`nohz_full` only stops the tick when **exactly one** task is runnable on that
core. Two runnable threads and the tick comes back, which is a classic
"I configured it and it did nothing" trap.

## Measuring

```bash
# Scheduling delay per task — the direct measurement:
perf sched record -- sleep 10 && perf sched latency --sort max

# Where did a thread go while it wasn't running (off-CPU analysis):
perf record -e sched:sched_switch -g -- ./app

# Per-task cumulative wait time:
cat /proc/<pid>/schedstat          # runtime, wait time, timeslices
cat /proc/<pid>/task/<tid>/status  # voluntary vs involuntary context switches

# Purpose-built RT latency measurement:
cyclictest -p 99 -t 4 -m -D 60 -h 400
```

`cyclictest` is the standard tool for characterising a system's worst-case
latency, and quoting it by name in an embedded/RT interview is expected.

**Voluntary versus involuntary context switches** in `/proc/<pid>/status` is a
fast, cheap diagnostic: high voluntary means you are blocking on something (I/O,
a lock); high involuntary means you are being preempted. They lead to completely
different investigations.

## Context switch cost

Direct cost is roughly 1-3 us: save/restore registers, switch `mm` (a `CR3` write
and possible TLB effects), scheduler bookkeeping.

The *indirect* cost is usually larger and is what people miss: the new task
evicts your cache lines and TLB entries, so when you resume you run cold. On a
working set that filled L2, the recovery can cost far more than the switch
itself. Thread-to-thread switches within one process are cheaper than process
switches because the address space and TLB entries survive.

That distinction — direct versus indirect cost — is a reliably good answer to
"how expensive is a context switch?"

## Interview angles

**"How does the Linux scheduler work?"**
CFS/EEVDF for normal tasks, RT classes above it, DEADLINE above that. Mention
EEVDF replaced CFS in 6.6.

**"Your pinned thread still sees 400 us spikes. Why?"**
Walk the seven suspects above. Name `isolcpus`, `nohz_full`, `rcu_nocbs` and IRQ
affinity together — most candidates name only `isolcpus`, and the incomplete
answer is visibly incomplete.

**"When would you use SCHED_FIFO? What's the risk?"**
Bounded-latency work; risk is starving everything else, hence `sched_rt_runtime_us`
and mandatory pinning.

**"How expensive is a context switch?"**
1-3 us direct, plus cache and TLB pollution that often dominates. Distinguish
thread-to-thread from process-to-process.

**"What is priority inversion and how is it solved?"**
Low-priority holder blocks a high-priority waiter while a medium-priority task
runs. Solved by priority inheritance (`PTHREAD_PRIO_INHERIT`) or priority ceiling.
Covered further in `13_embedded/01_rtos_design_constraints.md`.

## Exercises

1. Run `cyclictest -p 99 -t 4 -m -D 300` on a stock kernel. Record max latency.
   Then apply `isolcpus` + `nohz_full` + `rcu_nocbs` + IRQ affinity and rerun.
   Record the improvement. This is a genuinely impressive number to carry.
2. Write a thread that busy-polls and timestamps each iteration into a histogram.
   Run it pinned but without isolation, then with. Plot both tails on a log axis.
3. Deliberately create priority inversion with three `SCHED_FIFO` threads and a
   plain mutex. Observe the stall. Then enable `PTHREAD_PRIO_INHERIT` and observe
   it disappear.
4. Measure context switch cost directly: two threads ping-ponging on an `eventfd`
   or a pipe, pinned to the same core versus different cores. Explain the gap.
5. Check `/proc/<pid>/status` for a workload you own and classify its switches as
   voluntary or involuntary. Say what that implies before you profile further.
