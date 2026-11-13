# OS 04: CPU Scheduling Algorithms

*Part of the [Operating systems plan](00_plan.md) · Time: about 8 hours (5 reading and experiments, 3 problems) · You need: [OS 02](02_processes.md) (process states, context switch), [OS 03](03_threads.md) (threads, the cost of switching), and C 01 to C 06 for the code*

The scheduler answers one question: of the jobs that are ready, which one gets the CPU now, and for how long? Every operating-systems interview asks for the classic algorithms (FCFS, SJF, round robin, priority, multilevel feedback queue), asks you to work a numeric example by hand, and then asks the harder questions: why is shortest-job-first optimal and why can nobody build it, how do you choose a quantum, what is starvation and how does aging fix it, how does MLFQ approximate SJF without knowing the future, how do real-time schedulers prove deadlines are met, and what changes on several CPUs. This tutorial answers each with a scheduling simulator that was tested against an independent implementation, plus measurements of the real Linux scheduler on the machine it ran on. The Linux scheduler's internals (EEVDF, run queues, load balancing) are OS 05; here you learn the algorithms and see what Linux's behaviour looks like from the outside.

By the end you can:

- define turnaround, waiting and response time, throughput and utilisation, and compute them from a Gantt chart;
- work FCFS, SJF, SRTF, round robin, priority (with and without preemption) and MLFQ by hand on a workload, and state each algorithm's strength and failure case (convoy effect, starvation, quantum too small or too large);
- explain why SRTF gives the lowest average waiting time and why a real scheduler must estimate the next burst;
- choose a quantum from the trade-off between response time and switching overhead, with a measured table;
- explain starvation and aging, with a run that shows a job waiting for 22 ticks and then, with aging, for 12;
- state the five MLFQ rules and what each fixes;
- explain lottery and stride scheduling and the difference in their short-run fairness, with measured shares;
- decide whether a periodic task set is schedulable under rate-monotonic and EDF scheduling, by utilisation and by simulation;
- explain global versus per-CPU run queues, work stealing and CPU affinity, and show the effect on Linux;
- describe what Linux does (scheduling classes, nice weights, real-time policies and throttling) with numbers measured here.

**Where the numbers come from.** The scheduling algorithms are in a C simulator (`sim.h`, printed in section 3) and a real-time simulator (`rt.h`, section 10). Their results were compared with independent Python implementations on random inputs, plus hand-worked textbook answers; the results of those checks are in section 3. The Linux measurements were run on a Linux 6.18.44 kernel inside a Firecracker micro-VM (2 vCPUs, Intel Xeon at 2.30 GHz per `/proc/cpuinfo`, root access, Ubuntu userland, GCC 13.3, Python 3.13). Facts from the man pages, the kernel documentation and the OSTEP chapters were fetched and are linked in the Sources; facts from general knowledge are marked "from memory". `perf` was not installed.

**Data and timing.** Simulator results are exact (time is in whole ticks) and need no timing. The Linux experiments are wall-clock measurements on a cloud micro-VM with 2 vCPUs; where one ran twice the table shows run 1 / run 2 (two complete repetitions). With only 2 CPUs a few experiments are limited by the machine, and the text says so.

---

## 1. What the scheduler decides

A CPU runs one thread at a time. Many are **ready** (OS 02, state `R`). The **scheduler** is the policy that picks one; the **dispatcher** is the mechanism that switches the CPU to it (saves one context, restores another, returns to user mode). The scheduler runs when:

1. the running job **blocks** or **exits** (it must);
2. a job **becomes ready** (it was waiting for I/O or a lock, or just arrived), and may be more deserving than the running one;
3. a **timer interrupt** says the running job has used its time slice;
4. a job's priority changes.

If the scheduler only acts on case 1, it is **non-preemptive** (cooperative): a job keeps the CPU until it gives it up. If it also acts on 2 to 4 it is **preemptive**, and the kernel can take the CPU away. Every general-purpose OS today is preemptive; non-preemptive scheduling survives in simple embedded run-to-completion systems.

**Job types.** A **CPU-bound** job computes for long stretches; an **I/O-bound** (interactive) job computes for a short burst, then waits for a disk, a network packet or a keystroke. Good schedulers favour I/O-bound jobs: they use little CPU, and giving them the CPU quickly keeps the disks and the user busy. In this chapter's worked examples every job is CPU-bound (one burst, no I/O), which keeps the arithmetic checkable; MLFQ (section 8) is where the difference matters.

---

## 2. Metrics

For a job with arrival time `a`, CPU burst `b`, first run at `s` and completion at `f`:

| Metric | Definition | Meaning |
|---|---|---|
| Turnaround time | `f - a` | Total time in the system |
| Waiting time | `f - a - b` | Time spent ready but not running (for a one-burst job) |
| Response time | `s - a` | Time until the job first gets the CPU (what an interactive user feels) |
| Throughput | Jobs completed per unit time | System-wide |
| CPU utilisation | Fraction of time the CPU runs jobs | System-wide; context switches and idle time reduce it |
| Fairness | Each job gets its deserved share | No single number; shown below by share of CPU |

No algorithm wins on all of them. Minimising average turnaround favours short jobs; minimising response time favours frequent switching; maximising throughput means few switches. A scheduler is a point in that trade-off.

**The convoy effect.** If a long CPU-bound job holds the CPU first, every short job behind it waits for the whole burst, even though each could finish in a moment. Section 4 shows the numbers.

---

## 3. The simulator, and how it was tested

The code below is the entire simulator for the single-CPU algorithms. Time advances one tick at a time. Each tick it (1) admits arrivals, (2) decides whether the job that ran last should give up the CPU, (3) picks a job if the CPU is free, and (4) runs the chosen job for one tick. The rules the code implements:

- **Ties** go to the earlier arrival, then the lower job number.
- **Round robin:** a job that arrives in the same tick that another job's quantum expires goes into the queue **before** the preempted job. (This is the convention most textbooks use; some use the other order, which changes the answer. Always state it.)
- **SRTF and preemptive priority** re-choose every tick.
- **Priority aging:** a ready job that waits `age` ticks without running gets its priority number lowered by one (it keeps the improvement).
- **MLFQ:** three queues with quanta 2, 4 and 8; a new job enters the top queue; using a full quantum demotes a job one level; a job in a higher queue preempts one in a lower queue (the preempted job goes to the back of its own queue); no priority boost (section 8 adds it).

```c
#ifndef SIM_H
#define SIM_H
#include <stdint.h>
#include <string.h>

/* A tick-by-tick CPU scheduling simulator. Time is in whole ticks. Jobs are CPU bound (no I/O). */
#define MAXP 16
#define MAXT 4096
#define IDLE (-1)

typedef struct { int arrival, burst, prio; } Proc;      /* a smaller prio number is more important */
typedef enum { FCFS, SJF, SRTF, RR, PRIO_NP, PRIO_P, MLFQ } Policy;

typedef struct {
    int first[MAXP];                                    /* tick at which the job first ran */
    int finish[MAXP];                                   /* tick after its last run */
    int run[MAXT];                                      /* who ran in each tick (IDLE if nobody) */
    int len;                                            /* number of ticks simulated */
} Result;

static const int mlfq_quantum[3] = {2, 4, 8};

/* A small FIFO of job ids. */
typedef struct { int a[MAXP * 4]; int head, count; } Fifo;
static void fifo_push(Fifo *f, int v) { f->a[(f->head + f->count++) % (MAXP * 4)] = v; }
static int fifo_pop(Fifo *f) { int v = f->a[f->head]; f->head = (f->head + 1) % (MAXP * 4); f->count--; return v; }

/* Is job a better choice than job b under the key of this policy? Ties fall back to arrival, then id. */
static int better(Policy pol, const Proc *p, const int *rem, const int *eff, int a, int b)
{
    long ka, kb;
    switch (pol) {
    case SJF:     ka = p[a].burst; kb = p[b].burst; break;
    case SRTF:    ka = rem[a];     kb = rem[b];     break;
    case PRIO_NP:
    case PRIO_P:  ka = eff[a];     kb = eff[b];     break;
    default:      ka = kb = 0;                      break;      /* FCFS */
    }
    if (ka != kb) return ka < kb;
    if (p[a].arrival != p[b].arrival) return p[a].arrival < p[b].arrival;
    return a < b;
}

/* age > 0 (PRIO_P only): every `age` ticks a ready job spends without running lowers its priority number by one. */
static void sched_run(Policy pol, int quantum, int age, const Proc *p, int n, Result *r)
{
    int rem[MAXP], eff[MAXP], waited[MAXP], done = 0, cur = IDLE, used = 0, level[MAXP];
    Fifo rr = {{0}, 0, 0}, mq[3];
    memset(mq, 0, sizeof mq);
    for (int i = 0; i < n; ++i) { rem[i] = p[i].burst; eff[i] = p[i].prio; waited[i] = 0; r->first[i] = -1; r->finish[i] = -1; level[i] = 0; }
    int t = 0;
    for (; done < n && t < MAXT; ++t) {
        /* 1. arrivals join the queue (in id order) */
        for (int i = 0; i < n; ++i)
            if (p[i].arrival == t) {
                if (pol == RR) fifo_push(&rr, i);
                if (pol == MLFQ) { level[i] = 0; fifo_push(&mq[0], i); }
            }
        /* 2. the job that ran last tick: finished, out of quantum, or preempted? */
        if (pol == RR && cur != IDLE && used == quantum) { fifo_push(&rr, cur); cur = IDLE; }
        if (pol == MLFQ && cur != IDLE) {
            int demote = used == mlfq_quantum[level[cur]];
            int higher = 0;
            for (int l = 0; l < level[cur]; ++l) if (mq[l].count) higher = 1;
            if (demote && level[cur] < 2) level[cur]++;
            if (demote || higher) { fifo_push(&mq[level[cur]], cur); cur = IDLE; }
        }
        /* 3. choose a job if the CPU is free (or, for preemptive policies, re-choose every tick) */
        if (pol == SRTF || pol == PRIO_P) {
            int best = IDLE;
            for (int i = 0; i < n; ++i)
                if (p[i].arrival <= t && rem[i] > 0 && (best == IDLE || better(pol, p, rem, eff, i, best))) best = i;
            if (best != cur) used = 0;
            cur = best;
        } else if (cur == IDLE) {
            if (pol == RR) {
                if (rr.count) { cur = fifo_pop(&rr); used = 0; }
            } else if (pol == MLFQ) {
                for (int l = 0; l < 3 && cur == IDLE; ++l)
                    if (mq[l].count) { cur = fifo_pop(&mq[l]); used = 0; }
            } else {
                int best = IDLE;
                for (int i = 0; i < n; ++i)
                    if (p[i].arrival <= t && rem[i] > 0 && (best == IDLE || better(pol, p, rem, eff, i, best))) best = i;
                cur = best;
            }
        }
        /* 4. run one tick */
        r->run[t] = cur;
        if (cur != IDLE) {
            if (r->first[cur] < 0) r->first[cur] = t;
            --rem[cur];
            ++used;
            if (age > 0) waited[cur] = 0;
            if (rem[cur] == 0) { r->finish[cur] = t + 1; ++done; cur = IDLE; used = 0; }
        }
        if (age > 0)                                         /* aging: ready jobs that did not run get more important */
            for (int i = 0; i < n; ++i)
                if (p[i].arrival <= t && rem[i] > 0 && i != r->run[t]) {
                    if (++waited[i] % age == 0 && eff[i] > 0) --eff[i];
                }
    }
    r->len = t;
}
#endif
```

**How it was tested.** Section 2's formulas and the policies' rules were also implemented a second time in Python (`oracle.py`), with different code structure: event-driven for FCFS, SJF and priority, slice-by-slice for round robin, key-based selection for the preemptive policies. The two were compared on random workloads (1 to 7 jobs, arrivals 0 to 12, bursts 1 to 9, or up to 20 for MLFQ, priorities 0 to 4, quanta 1 to 5, aging 0 to 3). A comparison covers every job's first-run and finish times and the whole tick-by-tick run string. Hand-worked textbook answers were checked separately. Mutants (deliberate bugs in `sim.h`) check that the test would notice a defect. Result of the run:

```text
scheduler simulator against the Python oracle: 30000 random workloads, all 7 policies, results and run strings identical
known answer: FCFS 24,3,3      finish [24, 27, 30] expected [24, 27, 30] ok
known answer: SJF 7,4,1,4      finish [7, 12, 8, 16] expected [7, 12, 8, 16] ok
known answer: SRTF 7,4,1,4     finish [16, 7, 5, 11] expected [16, 7, 5, 11] ok
known answer: RR q=4 24,3,3    finish [30, 7, 10] expected [30, 7, 10] ok
real-time simulator against the exact tests (U <= 1 for EDF, response-time analysis for RM): 20000 random task sets, identical
mutants: 18, caught: 17
rt mutants: 6, caught: 5
```

The one mutant that escaped, "SJF keys on remaining time instead of burst", is an **equivalent mutant**: SJF (non-preemptive) only compares jobs that have not started, and for those remaining time equals burst, so the change cannot alter any output. Two other mutants escaped at first because the bursts in the random workloads were too short to expose them (a level-2 quantum of 4 against 8 only matters for a job that needs more than 6 ticks after being demoted twice); lengthening MLFQ bursts to 20 fixed that, and is the reason the generator does it. The real-time simulator of section 10 has its own test and one more equivalent mutant ("deadline one tick late" shifts every deadline by the same amount, so the earliest-deadline order does not change).

---

## 4. The classic algorithms on one workload

Four jobs: A arrives at 0 with burst 7, B at 2 with burst 4, C at 4 with burst 1, D at 5 with burst 4 (priorities A=2, B=1, C=4, D=3, smaller is more important). This program runs each policy and prints the Gantt chart as runs (`A0-7` is job A from tick 0 to 7), the finish times and the three averages:

```c
#include <stdio.h>
#include "sim.h"

static const char *names[] = {"FCFS", "SJF (non-preemptive)", "SRTF (preemptive SJF)", "Round robin", "Priority (non-preemptive)", "Priority (preemptive)", "MLFQ (2, 4, 8)"};

/* Gantt chart as runs: A0-7 means job A from tick 0 to 7. */
static void gantt(const Result *r)
{
    int start = 0;
    for (int t = 1; t <= r->len; ++t)
        if (t == r->len || r->run[t] != r->run[start]) {
            if (r->run[start] != IDLE) printf("%c%d-%d ", 'A' + r->run[start], start, t);
            start = t;
        }
    printf("\n");
}

static void report(const char *title, Policy pol, int q, int age, const Proc *p, int n)
{
    static Result r;
    sched_run(pol, q, age, p, n, &r);
    double tat = 0, wait = 0, resp = 0;
    int runs = 0;
    for (int i = 0; i < n; ++i) {
        tat += r.finish[i] - p[i].arrival;
        wait += r.finish[i] - p[i].arrival - p[i].burst;
        resp += r.first[i] - p[i].arrival;
    }
    for (int t = 0; t < r.len; ++t)
        if (r.run[t] != IDLE && (t == 0 || r.run[t] != r.run[t - 1])) ++runs;
    printf("%s\n  ", title);
    gantt(&r);
    printf("  finish:");
    for (int i = 0; i < n; ++i) printf(" %c=%d", 'A' + i, r.finish[i]);
    printf("\n  average turnaround %.2f, waiting %.2f, response %.2f, dispatches %d\n", tat / n, wait / n, resp / n, runs);
}

int main(void)
{
    /* arrival, burst, priority (smaller = more important) */
    Proc p[4] = {{0, 7, 2}, {2, 4, 1}, {4, 1, 4}, {5, 4, 3}};
    printf("Workload: A(arrive 0, burst 7, prio 2) B(2, 4, prio 1) C(4, 1, prio 4) D(5, 4, prio 3)\n\n");
    report(names[FCFS], FCFS, 0, 0, p, 4);
    report(names[SJF], SJF, 0, 0, p, 4);
    report(names[SRTF], SRTF, 0, 0, p, 4);
    report("Round robin, quantum 2", RR, 2, 0, p, 4);
    report("Round robin, quantum 4", RR, 4, 0, p, 4);
    report(names[PRIO_NP], PRIO_NP, 0, 0, p, 4);
    report(names[PRIO_P], PRIO_P, 0, 0, p, 4);
    report(names[MLFQ], MLFQ, 0, 0, p, 4);
    return 0;
}
```

```text
Workload: A(arrive 0, burst 7, prio 2) B(2, 4, prio 1) C(4, 1, prio 4) D(5, 4, prio 3)

FCFS
  A0-7 B7-11 C11-12 D12-16
  finish: A=7 B=11 C=12 D=16
  average turnaround 8.75, waiting 4.75, response 4.75, dispatches 4
SJF (non-preemptive)
  A0-7 C7-8 B8-12 D12-16
  finish: A=7 B=12 C=8 D=16
  average turnaround 8.00, waiting 4.00, response 4.00, dispatches 4
SRTF (preemptive SJF)
  A0-2 B2-4 C4-5 B5-7 D7-11 A11-16
  finish: A=16 B=7 C=5 D=11
  average turnaround 7.00, waiting 3.00, response 0.50, dispatches 6
Round robin, quantum 2
  A0-2 B2-4 A4-6 C6-7 B7-9 D9-11 A11-13 D13-15 A15-16
  finish: A=16 B=9 C=7 D=15
  average turnaround 9.00, waiting 5.00, response 1.50, dispatches 9
Round robin, quantum 4
  A0-4 B4-8 C8-9 A9-12 D12-16
  finish: A=12 B=8 C=9 D=16
  average turnaround 8.50, waiting 4.50, response 3.25, dispatches 5
Priority (non-preemptive)
  A0-7 B7-11 D11-15 C15-16
  finish: A=7 B=11 C=16 D=15
  average turnaround 9.50, waiting 5.50, response 5.50, dispatches 4
Priority (preemptive)
  A0-2 B2-6 A6-11 D11-15 C15-16
  finish: A=11 B=6 C=16 D=15
  average turnaround 9.25, waiting 5.25, response 4.25, dispatches 5
MLFQ (2, 4, 8)
  A0-2 B2-4 C4-5 D5-7 A7-11 B11-13 D13-15 A15-16
  finish: A=16 B=13 C=5 D=15
  average turnaround 9.50, waiting 5.50, response 0.00, dispatches 8
```

Summary of the same run:

| Algorithm | Avg turnaround | Avg waiting | Avg response | Dispatches |
|---|---|---|---|---|
| FCFS | 8.75 | 4.75 | 4.75 | 4 |
| SJF (non-preemptive) | 8.00 | 4.00 | 4.00 | 4 |
| SRTF | 7.00 | 3.00 | 0.50 | 6 |
| Round robin, quantum 2 | 9.00 | 5.00 | 1.50 | 9 |
| Round robin, quantum 4 | 8.50 | 4.50 | 3.25 | 5 |
| Priority, non-preemptive | 9.50 | 5.50 | 5.50 | 4 |
| Priority, preemptive | 9.25 | 5.25 | 4.25 | 5 |
| MLFQ (2, 4, 8) | 9.50 | 5.50 | 0.00 | 8 |

(A dispatch is one maximal run on the chart. A job that is alone keeps the CPU without a new dispatch.)

Check one by hand. FCFS runs A to 7, then B 7 to 11, C 11 to 12, D 12 to 16. Waiting times: A 0, B `11-2-4 = 5`, C `12-4-1 = 7`, D `16-5-4 = 7`; average `19/4 = 4.75`. Matches.

### 4.1 FCFS (first come, first served)

Run jobs in arrival order, no preemption. **Strength:** the simplest to implement, no starvation, one queue. **Weakness:** the convoy effect. C needs one tick and waited 7. With a long job first it is worse: three jobs of 24, 3 and 3 ticks arriving together give waiting times 0, 24 and 27 (average 17 in the order 24, 3, 3) and 0, 3 and 6 in the order 3, 3, 24 (average 3). The order alone changed the average waiting time by a factor of almost six.

### 4.2 SJF and SRTF (shortest job first)

SJF runs the shortest ready job next; **SRTF** (shortest remaining time first) is the preemptive version: when a job arrives with less work than the running job has left, it takes the CPU. The result above: SJF 4.00 against FCFS 4.75, SRTF 3.00.

**SRTF has the lowest possible average waiting time** of any algorithm on this workload (shown from memory by an exchange argument: if a longer job runs while a shorter one waits, swapping their order lowers the total waiting time, and a preemptive version applies the swap at every arrival). SJF is optimal among non-preemptive algorithms. The measured table agrees: 3.00 is the lowest waiting time in the column.

**The catch:** the scheduler does not know the burst length. Real systems **estimate** the next CPU burst from the history of the previous ones, usually an exponential average: `estimate_next = alpha * last_burst + (1 - alpha) * estimate_old` with `alpha` around 0.5 (from memory). And SRTF can **starve long jobs**: a steady stream of short jobs keeps the long one waiting forever.

### 4.3 Round robin

Each job gets the CPU for at most one **quantum** (time slice), then goes to the back of the queue. **Strength:** every job is served within `(n - 1) * q` ticks of becoming ready, so response time is bounded and good (1.50 at q=2, against 4.75 for FCFS). **Weakness:** turnaround is worse than SJF for jobs of unequal length, and the switching costs time. With `q` very large it becomes FCFS; with `q` very small it spends all its time switching. Section 5 measures the trade-off.

Trace of the tie rule (Problem B): A arrives at 0 with burst 5, B at 1 with burst 3, C at 4 with burst 2, quantum 2. At tick 4 B's quantum expires and C arrives in the same tick; with the rule above the order is `A A B B A A C C B A`: C goes ahead of the preempted B.

### 4.4 Priority scheduling

Each job has a priority; the highest-priority ready job runs. Non-preemptive: the running job keeps the CPU until it ends. Preemptive: a newly ready higher-priority job takes the CPU at once. Priorities come from outside (importance, user, deadline) or from inside (the system raises I/O-bound jobs). **Weakness: starvation**, section 6.

### 4.5 Two more that exam-style questions use: HRRN and the multilevel queue

**HRRN (highest response ratio next)** is non-preemptive. When the CPU becomes free it computes, for every ready job, the ratio `(waiting time + burst) / burst` and runs the job with the largest ratio. A short job starts with a high ratio, as in SJF, but a long job's ratio grows the longer it waits, so it cannot be postponed for ever. This is aging built into the formula. These hand-worked examples were checked with a short script, which is not printed here:

- On the section 4 workload, at tick 7 the ratios are B `(5+4)/4 = 2.25`, C `(3+1)/1 = 4.0` and D `(2+4)/4 = 1.5`, so C runs. At tick 8 they are B 2.5 and D 1.75, so B runs, then D. The schedule and the averages (turnaround 8.00, waiting 4.00) are the same as SJF's here.
- The difference shows with a stream of short jobs. A arrives at 0 with burst 3 and L at 1 with burst 6. Six 2-tick jobs arrive at 2, 4, 6, 8, 10 and 12. Under SJF, L always loses to a 2-tick job and finishes **last, at 21**. Under HRRN, at tick 5 L's ratio is `(4+6)/6 = 1.67` against 1.5 for the waiting short job, so L runs and finishes **at 11**. The short jobs finish later (the last at 21 instead of 15). HRRN also needs burst lengths, so it has SJF's estimation problem.

**Multilevel queue** (without feedback) splits the ready jobs into fixed classes, for example interactive (foreground) and batch (background). Each class has its own queue and its own algorithm, often round robin for the foreground and FCFS for the background. Between the queues the scheduler uses either strict priority (background runs only when the foreground queue is empty, so it can starve) or a time split (for example 80 percent of the CPU to the foreground). A job never changes class. Letting it change class based on its behaviour is the step that turns this into the MLFQ of section 8. Linux's scheduling classes (section 9) are a multilevel queue with strict priority between classes.

**Real jobs alternate CPU and I/O bursts.** Every job in this chapter's examples is one CPU burst. A real interactive job computes for a millisecond, then waits for I/O. A scheduler treats each CPU burst as a separate job, so while one job waits for the disk another runs: CPU and I/O overlap. This is why favouring short bursts (SJF, MLFQ) helps interactive jobs: they get the CPU back quickly and their next I/O starts sooner.

---

## 5. Choosing the quantum

If the quantum is too small the CPU spends its time on dispatches (each one saves and restores a context and disturbs caches and the TLB, OS 02); if it is too large round robin becomes FCFS and response time suffers. The measured trade-off, with a switch cost of one tick per dispatch, for five jobs of 3, 6, 9, 12 and 40 ticks that all arrive at tick 0:

```c
#include <stdio.h>
#include "sim.h"

/* The cost of a small quantum: five jobs arrive together; every dispatch costs `cost` ticks of switching. */
int main(void)
{
    Proc p[5] = {{0, 3, 0}, {0, 6, 0}, {0, 9, 0}, {0, 12, 0}, {0, 40, 0}};
    int cost = 1;
    static Result r;
    printf("jobs: bursts 3, 6, 9, 12, 40, all arrive at 0; each dispatch costs %d tick(s)\n", cost);
    printf("quantum  dispatches | no switch cost: response  turnaround | with switch cost: response  turnaround\n");
    int qs[] = {1, 2, 4, 8, 16, 64};
    for (int k = 0; k < 6; ++k) {
        sched_run(RR, qs[k], 0, p, 5, &r);
        int disp = 0, at[MAXT];                         /* at[t] = dispatches that began at or before tick t */
        for (int t = 0; t < r.len; ++t) {
            if (r.run[t] != IDLE && (t == 0 || r.run[t] != r.run[t - 1])) ++disp;
            at[t] = disp;
        }
        double resp = 0, tat = 0, resp_c = 0, tat_c = 0;
        for (int i = 0; i < 5; ++i) {
            resp += r.first[i];
            tat += r.finish[i];
            resp_c += r.first[i] + cost * at[r.first[i]];            /* the real start: every dispatch so far cost `cost` */
            tat_c += r.finish[i] + cost * at[r.finish[i] - 1];
        }
        printf("%7d  %10d | %24.1f %11.1f | %26.1f %11.1f\n", qs[k], disp, resp / 5, tat / 5, resp_c / 5, tat_c / 5);
    }
    return 0;
}
```

```text
jobs: bursts 3, 6, 9, 12, 40, all arrive at 0; each dispatch costs 1 tick(s)
quantum  dispatches | no switch cost: response  turnaround | with switch cost: response  turnaround
      1          42 |                      2.0        36.0 |                        5.0        66.4
      2          22 |                      4.0        35.2 |                        7.0        50.8
      4          12 |                      7.2        33.2 |                       10.2        41.2
      8           8 |                     10.8        30.8 |                       13.8        35.6
     16           5 |                     12.0        26.0 |                       15.0        29.0
     64           5 |                     12.0        26.0 |                       15.0        29.0
```

How to read it:

- **Response time grows with the quantum**: 2.0 ticks at q=1, 12.0 at q=16 (where it is FCFS). The smaller the quantum the sooner every job first runs.
- **Turnaround falls with the quantum here**: 36.0 at q=1 down to 26.0 at q=16. For jobs that all arrive together and are listed shortest first, FCFS is the same as SJF and gives the best turnaround; round robin finishes everything late because every job is carried along to the end.
- **The switch cost makes a small quantum much worse**: with a one-tick cost the average turnaround at q=1 is 66.4 against 36.0 without it, because 42 dispatches each cost a tick; at q=16 only 5 dispatches add 3.
- So the quantum is a **compromise**: large enough that switching is a small fraction of the slice, small enough for acceptable response. The usual rule of thumb is that about 80 percent of CPU bursts should be shorter than the quantum (from memory), and typical values are 10 to 100 ms. Linux's real-time round robin quantum, measured in section 11, is 100 ms.

---

## 6. Starvation and aging

**Starvation** (indefinite blocking) means a ready job never gets the CPU. It happens in SRTF and in priority scheduling when a stream of more favoured jobs keeps arriving. The program below creates one low-priority job of 5 ticks and a stream of eleven 2-tick high-priority jobs, one arriving every 2 ticks, so the CPU is never free for the victim; then it repeats with **aging** (every 3 ticks that a job waits, its priority number drops by one):

```c
#include <stdio.h>
#include "sim.h"

/* A steady stream of short high-priority jobs, and one low-priority job that arrived first. */
int main(void)
{
    Proc p[12];
    p[0] = (Proc){0, 5, 5};                              /* the victim: low priority, 5 ticks of work */
    for (int i = 1; i < 12; ++i)
        p[i] = (Proc){2 * (i - 1), 2, 1};                /* a new 2-tick high-priority job every 2 ticks: the CPU is never free for it */
    static Result r;
    int ages[] = {0, 3};
    for (int k = 0; k < 2; ++k) {
        sched_run(PRIO_P, 0, ages[k], p, 12, &r);
        printf("aging %-9s: the low-priority job first runs at tick %2d and finishes at tick %2d (the last high-priority job finishes at tick %d)\n",
               ages[k] ? "every 3" : "off", r.first[0], r.finish[0], r.finish[11]);
    }
    return 0;
}
```

```text
aging off      : the low-priority job first runs at tick 22 and finishes at tick 27 (the last high-priority job finishes at tick 22)
aging every 3  : the low-priority job first runs at tick 12 and finishes at tick 20 (the last high-priority job finishes at tick 27)
```

Without aging the low-priority job does not run until every high-priority job has finished (tick 22). With aging its priority number falls by one every 3 waiting ticks, it reaches the priority of the stream after 12 ticks, ties are broken by arrival (it arrived first) and it runs at tick 12, 10 ticks sooner, and the stream finishes later (27 instead of 22) because the victim took its 5 ticks. Aging does not remove the trade-off; it limits how long a job can be postponed.

**Priority inversion** (from memory; the full treatment is OS 07): a high-priority job waits for a lock held by a low-priority job, and a medium-priority job keeps running, so the low-priority job never releases the lock. The standard fix is **priority inheritance**: the lock holder temporarily takes the priority of the highest waiter.

---

## 7. Proportional share: lottery and stride

Instead of strict priority, give each job a share. In **lottery scheduling** each job holds tickets; each quantum the scheduler draws a random ticket number and the holder runs. A job with 75 of 100 tickets should receive about 75 percent of the CPU ([OSTEP, lottery scheduling](https://pages.cs.wisc.edu/~remzi/OSTEP/cpu-sched-lottery.pdf), fetched). Randomness makes it simple (no per-job state beyond tickets) but only **probabilistically** fair. **Stride scheduling** is the deterministic version: each job has a stride equal to a large constant divided by its tickets, and a pass value that starts at 0; the scheduler runs the job with the smallest pass value and then adds the job's stride to it. The same source gives its weakness: it needs global state, and the starting pass value of a newly arrived job is a problem (zero would let it monopolise the CPU).

```c
#include <stdio.h>
#include <stdint.h>

static uint64_t rs = 88172645463325252ull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 11); }

/* Lottery: each quantum, draw a ticket number and run its owner. */
static void lottery(const int *tickets, int n, int quanta, long *got)
{
    int total = 0;
    for (int i = 0; i < n; ++i) { total += tickets[i]; got[i] = 0; }
    for (int q = 0; q < quanta; ++q) {
        int pick = (int)(rnd() % (uint32_t)total), i = 0;
        while (pick >= tickets[i]) { pick -= tickets[i]; ++i; }
        ++got[i];
    }
}

/* Stride: stride = BIG / tickets; always run the job with the smallest pass value, then add its stride. */
static void stride(const int *tickets, int n, int quanta, long *got)
{
    const long BIG = 1000000;
    long pass[8], step[8];
    for (int i = 0; i < n; ++i) { step[i] = BIG / tickets[i]; pass[i] = 0; got[i] = 0; }
    for (int q = 0; q < quanta; ++q) {
        int best = 0;
        for (int i = 1; i < n; ++i)
            if (pass[i] < pass[best]) best = i;
        ++got[best];
        pass[best] += step[best];
    }
}

int main(void)
{
    int tickets[3] = {100, 200, 700};
    long got[3];
    int sizes[] = {10, 100, 100000};
    printf("tickets 100 / 200 / 700 (ideal shares 10%% / 20%% / 70%%)\n");
    for (int k = 0; k < 3; ++k) {
        lottery(tickets, 3, sizes[k], got);
        printf("lottery, %6d quanta: %6ld %6ld %6ld  = %5.1f%% %5.1f%% %5.1f%%\n", sizes[k], got[0], got[1], got[2],
               100.0 * got[0] / sizes[k], 100.0 * got[1] / sizes[k], 100.0 * got[2] / sizes[k]);
    }
    for (int k = 0; k < 3; ++k) {
        stride(tickets, 3, sizes[k], got);
        printf("stride,  %6d quanta: %6ld %6ld %6ld  = %5.1f%% %5.1f%% %5.1f%%\n", sizes[k], got[0], got[1], got[2],
               100.0 * got[0] / sizes[k], 100.0 * got[1] / sizes[k], 100.0 * got[2] / sizes[k]);
    }
    return 0;
}
```

```text
tickets 100 / 200 / 700 (ideal shares 10% / 20% / 70%)
lottery,     10 quanta:      1      2      7  =  10.0%  20.0%  70.0%
lottery,    100 quanta:      7     26     67  =   7.0%  26.0%  67.0%
lottery, 100000 quanta:  10093  19784  70123  =  10.1%  19.8%  70.1%
stride,      10 quanta:      1      2      7  =  10.0%  20.0%  70.0%
stride,     100 quanta:     10     20     70  =  10.0%  20.0%  70.0%
stride,  100000 quanta:   9998  19994  70008  =  10.0%  20.0%  70.0%
```

Three jobs with 100, 200 and 700 tickets, ideal shares 10, 20 and 70 percent:

- Lottery over **100,000 quanta** came within a fraction of a percent (10.1, 19.8, 70.1). Over **100 quanta** it was off by several points (7, 26, 67). Over 10 quanta the outcome (1, 2, 7) happened to be exact, which is luck, not design (rerun with a different seed to see it vary).
- **Stride** gave exactly 10, 20 and 70 for 100 quanta, and within 6 quanta of the ideal over 100,000, because it is deterministic.
- Lesson: lottery is fair in the long run and noisy in the short run; stride is fair at every scale and needs more state. Linux's EEVDF (section 11) is a descendant of this idea: it tracks how much service each task is owed.

---

## 8. Multilevel feedback queue (MLFQ)

MLFQ solves two problems at once: run short interactive jobs first (to minimise response, like SJF) **without knowing** their lengths, and still make progress on long jobs. It learns a job's type from its behaviour. The final rules from [OSTEP's MLFQ chapter](https://pages.cs.wisc.edu/~remzi/OSTEP/cpu-sched-mlfq.pdf) (fetched, paraphrased):

1. A job at a higher priority level runs before one at a lower level.
2. Jobs at the same level take turns, round robin.
3. A new job starts at the top level.
4. A job that uses up its **allotment** at a level (however many times it gave up the CPU along the way) moves down one level.
5. After some interval, every job is moved back to the top level (**priority boost**).

How each rule matters: rules 1 and 2 are the basic structure; rule 3 assumes every job is interactive until shown otherwise, so a short job finishes at the top quickly; rule 4 pushes a job that keeps computing down to where it cannot delay the others, and the "however many times" wording defeats a program that issues a cheap I/O call just before its quantum ends to stay at the top (**gaming the scheduler**); rule 5 stops **starvation** of long jobs and lets a job whose behaviour changed (from CPU-bound to interactive) move up again.

In the table of section 4, MLFQ (with quanta 2, 4 and 8 and no boost) has response time **0.00**: every job got the CPU the tick it arrived because new jobs enter at the top level and preempt lower levels. Its turnaround (9.50) is the worst of the group on this CPU-bound workload: MLFQ guesses SJF behaviour and guesses wrong when jobs do not block.

**The priority boost, shown (Problem E).** A long job of 12 ticks arrives first; a stream of fifteen 2-tick jobs arrives one every 2 ticks and never leaves the top level, so the long job, demoted after its first quantum, is starved. The program checks that its boost-free MLFQ agrees with `sim.h`, then adds a boost every 10 ticks:

```c
#include <stdio.h>
#include <string.h>
#include "sim.h"

/* Problem E: MLFQ with a periodic priority boost. Same rules as sim.h (levels 0..2, quanta 2, 4, 8, demote on a full quantum,
   a higher queue preempts), plus: every `boost` ticks every unfinished job moves to level 0. boost == 0 means no boost. */
static void mlfq_boost(const Proc *p, int n, int boost, int *finish, int *long_ticks_by_30)
{
    int rem[MAXP], level[MAXP], cur = IDLE, used = 0, done = 0;
    *long_ticks_by_30 = 0;
    Fifo mq[3];
    memset(mq, 0, sizeof mq);
    for (int i = 0; i < n; ++i) { rem[i] = p[i].burst; level[i] = 0; finish[i] = -1; }
    for (int t = 0; done < n && t < MAXT; ++t) {
        for (int i = 0; i < n; ++i)
            if (p[i].arrival == t) fifo_push(&mq[0], i);
        if (cur != IDLE) {
            int demote = used == mlfq_quantum[level[cur]], higher = 0;
            for (int l = 0; l < level[cur]; ++l) if (mq[l].count) higher = 1;
            if (demote && level[cur] < 2) level[cur]++;
            if (demote || higher) { fifo_push(&mq[level[cur]], cur); cur = IDLE; }
        }
        if (boost > 0 && t > 0 && t % boost == 0) {          /* the boost: everything back to the top queue, in queue order */
            if (cur != IDLE) { fifo_push(&mq[level[cur]], cur); cur = IDLE; }
            Fifo top = {{0}, 0, 0};
            for (int l = 0; l < 3; ++l)
                while (mq[l].count) { int j = fifo_pop(&mq[l]); level[j] = 0; fifo_push(&top, j); }
            mq[0] = top;
        }
        if (cur == IDLE)
            for (int l = 0; l < 3 && cur == IDLE; ++l)
                if (mq[l].count) { cur = fifo_pop(&mq[l]); used = 0; }
        if (cur == 0 && t < 30) ++*long_ticks_by_30;          /* how much service job 0 (the long job) had received by tick 30 */
        if (cur != IDLE) {
            --rem[cur];
            ++used;
            if (rem[cur] == 0) { finish[cur] = t + 1; ++done; cur = IDLE; used = 0; }
        }
    }
}

int main(void)
{
    Proc p[16];
    p[0] = (Proc){0, 12, 0};                             /* one long job */
    for (int i = 1; i < 16; ++i)
        p[i] = (Proc){2 * (i - 1), 2, 0};                /* a stream of 2-tick jobs, one every 2 ticks: they never leave level 0 */
    int f_sim[16], f0[16], f10[16], g0, g10;
    static Result r;
    sched_run(MLFQ, 0, 0, p, 16, &r);
    for (int i = 0; i < 16; ++i) f_sim[i] = r.finish[i];
    mlfq_boost(p, 16, 0, f0, &g0);
    mlfq_boost(p, 16, 10, f10, &g10);
    int same = 1;
    for (int i = 0; i < 16; ++i) same &= f_sim[i] == f0[i];
    printf("boost off agrees with the simulator for all 16 jobs: %s\n", same ? "yes" : "NO");
    printf("ticks of service the long job (burst 12) has had by tick 30: %d with no boost, %d with a boost every 10 ticks\n", g0, g10);
    printf("long job finishes at tick %d with no boost, %d with a boost\n", f0[0], f10[0]);
    int last0 = 0, last10 = 0;
    for (int i = 1; i < 16; ++i) { if (f0[i] > last0) last0 = f0[i]; if (f10[i] > last10) last10 = f10[i]; }
    printf("the last short job finishes at tick %d with no boost, %d with a boost\n", last0, last10);
    return 0;
}
```

```text
boost off agrees with the simulator for all 16 jobs: yes
ticks of service the long job (burst 12) has had by tick 30: 2 with no boost, 6 with a boost every 10 ticks
long job finishes at tick 42 with no boost, 42 with a boost
the last short job finishes at tick 32 with no boost, 36 with a boost
```

The long job had 2 ticks of service by tick 30 without the boost and 6 with it, at the price of finishing the last short job later (36 instead of 32). The long job still finishes at tick 42 either way, because the CPU is never idle and that job is the last to finish: total work is 42 ticks. So the boost redistributes delay; it does not create CPU time. Choosing the interval is the hard part: too long and the starvation remains, too short and interactive jobs lose time to the boosted ones (the OSTEP chapter makes the same point).

---

## 9. What Linux does with these ideas (a first look)

Linux's scheduler is OS 05. From outside it looks like this (all measured on this machine):

**Nice values are weights.** Two CPU-bound processes pinned to one CPU with different nice values split the CPU by weight. According to [sched(7)](https://man7.org/linux/man-pages/man7/sched.7.html) (fetched) each nice step is roughly a factor of 1.25 in CPU share. The program starts two spinners on CPU 1 and compares the work they get done in 4 seconds:

```c
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

/* Two CPU-bound processes, pinned to the same CPU, with different nice values. Each counts loop iterations for 4 s. */
static unsigned long spin(int nice_value, double seconds)
{
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(1, &s);
    sched_setaffinity(0, sizeof s, &s);
    setpriority(PRIO_PROCESS, 0, nice_value);
    unsigned long n = 0;
    double end = now_s() + seconds;
    while (now_s() < end)
        for (int i = 0; i < 1000; ++i)
            ++n;
    return n;
}

int main(int argc, char **argv)
{
    int nice_b = argc > 1 ? atoi(argv[1]) : 10;
    int fd[2];
    pipe(fd);
    for (int k = 0; k < 2; ++k) {
        if (fork() == 0) {
            unsigned long n = spin(k == 0 ? 0 : nice_b, 4.0);
            write(fd[1], &n, sizeof n);
            _exit(0);
        }
    }
    unsigned long a, b;
    read(fd[0], &a, sizeof a);
    read(fd[0], &b, sizeof b);
    wait(NULL);
    wait(NULL);
    if (a < b) { unsigned long t = a; a = b; b = t; }           /* the faster is the nice 0 one */
    printf("two CPU-bound processes on one CPU, nice 0 and nice %d, 4 s: work done ratio %.2f to 1\n", nice_b, (double)a / (double)b);
    return 0;
}
```

```text
nice 0 against nice 10, run 1:
two CPU-bound processes on one CPU, nice 0 and nice 10, 4 s: work done ratio 9.13 to 1
nice 0 against nice 10, run 2:
two CPU-bound processes on one CPU, nice 0 and nice 10, 4 s: work done ratio 9.25 to 1
nice 0 against nice 5:
two CPU-bound processes on one CPU, nice 0 and nice 5, 4 s: work done ratio 3.06 to 1
```

The prediction from 1.25 per step is `1.25^10 = 9.3` and `1.25^5 = 3.05`; the measurements were 9.13 and 9.25 (nice 10) and 3.06 (nice 5). The 1.25 factor is how CFS and its successor map the 40 nice levels to weights (the weight table is from memory). This is the proportional-share idea of section 7 in a production kernel: nice is not a priority in the strict sense, it is a share.

**Real-time policies preempt everything normal.** `SCHED_FIFO` and `SCHED_RR` have priorities 1 to 99 and always outrank the normal policies (`SCHED_OTHER`, `SCHED_BATCH`, `SCHED_IDLE`, which use priority 0); the man page states that `SCHED_FIFO` has no time slicing and that a runaway loop can starve other threads. The measured ranges:

```c
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <time.h>

int main(void)
{
    struct { const char *name; int pol; } p[] = {{"SCHED_OTHER", SCHED_OTHER}, {"SCHED_BATCH", SCHED_BATCH}, {"SCHED_IDLE", SCHED_IDLE},
                                                  {"SCHED_FIFO", SCHED_FIFO}, {"SCHED_RR", SCHED_RR}};
    for (int i = 0; i < 5; ++i)
        printf("%-12s priority range %2d to %2d\n", p[i].name, sched_get_priority_min(p[i].pol), sched_get_priority_max(p[i].pol));
    struct timespec ts;
    sched_rr_get_interval(0, &ts);
    printf("SCHED_OTHER time slice reported for this process: %ld.%03ld ms\n", ts.tv_sec * 1000 + ts.tv_nsec / 1000000, (ts.tv_nsec / 1000) % 1000);
    struct sched_param sp = {.sched_priority = 1};
    sched_setscheduler(0, SCHED_RR, &sp);
    sched_rr_get_interval(0, &ts);
    printf("SCHED_RR quantum reported after switching this process to SCHED_RR: %ld.%03ld ms\n", ts.tv_sec * 1000 + ts.tv_nsec / 1000000, (ts.tv_nsec / 1000) % 1000);
    return 0;
}
```

```text
SCHED_OTHER  priority range  0 to  0
SCHED_BATCH  priority range  0 to  0
SCHED_IDLE   priority range  0 to  0
SCHED_FIFO   priority range  1 to 99
SCHED_RR     priority range  1 to 99
SCHED_OTHER time slice reported for this process: 0.000 ms
SCHED_RR quantum reported after switching this process to SCHED_RR: 100.000 ms
```

`SCHED_RR` reports a 100 ms quantum on this kernel (the man page says the quantum can be queried with `sched_rr_get_interval` and gives no default, so 100 ms is this measurement; for a `SCHED_OTHER` process the call reports 0, because the normal scheduler has no fixed quantum).

**Starvation, measured, and its safety valve.** A real-time hog runs on CPU 1 and a normal process on the same CPU measures how much CPU it gets in 6 seconds:

```c
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static void pin(int cpu)
{
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(cpu, &s);
    sched_setaffinity(0, sizeof s, &s);
}

/* A normal process measures how much CPU it gets on CPU 1 in 6 s while a real-time hog is also on CPU 1. */
int main(int argc, char **argv)
{
    int policy = argc > 1 ? (argv[1][0] == 'f' ? SCHED_FIFO : argv[1][0] == 'r' ? SCHED_RR : SCHED_OTHER) : SCHED_OTHER;
    const char *name = policy == SCHED_FIFO ? "SCHED_FIFO" : policy == SCHED_RR ? "SCHED_RR" : "SCHED_OTHER";
    pid_t hog = fork();
    if (hog == 0) {
        pin(1);
        struct sched_param sp = {.sched_priority = policy == SCHED_OTHER ? 0 : 10};
        if (sched_setscheduler(0, policy, &sp) != 0) perror("sched_setscheduler");
        double end = now_s() + 9.0;
        while (now_s() < end)
            ;
        _exit(0);
    }
    pid_t victim = fork();
    if (victim == 0) {
        pin(1);                                         /* normal policy, nice 0 */
        usleep(200000);                                 /* let the hog start first */
        double t0 = now_s(), stolen = 0, prev = t0;
        /* sample the clock in a tight loop; a gap longer than 100 us between two samples means this process was not on the CPU */
        while (now_s() - t0 < 6.0) {
            double cur = now_s();
            if (cur - prev > 0.0001) stolen += cur - prev;
            prev = cur;
        }
        double elapsed = now_s() - t0;
        printf("hog policy %-11s: the normal process got %5.1f%% of the CPU in a 6 s window on the same CPU as the hog\n", name, 100.0 * (elapsed - stolen) / elapsed);
        fflush(stdout);                                 /* _exit does not flush stdio */
        _exit(0);
    }
    waitpid(victim, NULL, 0);
    waitpid(hog, NULL, 0);
    return 0;
}
```

Run 1:

```text
hog policy SCHED_OTHER: the normal process got  49.0% of the CPU in a 6 s window on the same CPU as the hog
hog policy SCHED_FIFO : the normal process got   5.3% of the CPU in a 6 s window on the same CPU as the hog
hog policy SCHED_RR   : the normal process got  21.1% of the CPU in a 6 s window on the same CPU as the hog
```

Run 2:

```text
hog policy SCHED_OTHER: the normal process got  49.6% of the CPU in a 6 s window on the same CPU as the hog
hog policy SCHED_FIFO : the normal process got   5.2% of the CPU in a 6 s window on the same CPU as the hog
hog policy SCHED_RR   : the normal process got   5.3% of the CPU in a 6 s window on the same CPU as the hog
```

- Against another normal process (`SCHED_OTHER`) the victim got **about half** the CPU (49 percent): fair sharing between two equals.
- Against a `SCHED_FIFO` hog it got **5.2 to 5.3 percent**. That is not starvation to zero because Linux **throttles** real-time tasks: `sched_rt_period_us` is 1,000,000 and `sched_rt_runtime_us` is 950,000 here, so real-time tasks may use at most 95 percent of each second and normal tasks keep 5 percent (the man page states these defaults). The measurement matches the setting. Without the throttle (`sched_rt_runtime_us` set to -1) a real-time infinite loop would lock the CPU.
- `SCHED_RR` gave 5.3 percent in run 2 but **21.1 percent in run 1**. I did not find out why. An earlier version of this experiment with a 3-second window also gave about 27 percent for `SCHED_FIFO` against 5.3 percent for a 6-second window, so short windows are unreliable here: the throttling works in 1-second periods and a window of a few periods is sensitive to where it falls. Treat the 5 percent as the steady-state figure and the single 21 percent as an unexplained outlier.

---

## 10. Real-time scheduling

A **real-time** system must finish work by a **deadline**; correctness depends on time. **Hard** real-time means a missed deadline is a failure (a brake controller); **soft** means it only degrades quality (video playback). The typical workload is **periodic**: a task releases a job every `T` ticks needing at most `C` ticks of CPU, to be done before the next release.

**Utilisation.** `U = sum(C_i / T_i)`. If `U > 1` no scheduler can meet all deadlines on one CPU.

**EDF (earliest deadline first):** always run the job with the nearest deadline. For periodic tasks with deadline equal to period on one CPU, **EDF meets every deadline if and only if `U <= 1`** (from memory; the Liu and Layland result, used as the oracle below). **Rate-monotonic (RM):** fixed priorities, the shorter the period the higher the priority. It is simpler and predictable but less efficient: a sufficient (not necessary) test is `U <= n * (2^(1/n) - 1)`, which tends to 0.693 as `n` grows (from memory; the program computes it). A task set above the bound may or may not be schedulable; an exact test is response-time analysis.

```c
#ifndef RT_H
#define RT_H
#define MAXTASK 8

/* Periodic real-time tasks: every `period` ticks a job of `wcet` ticks is released and must finish before the next release. */
typedef struct { int wcet, period; } Task;
typedef enum { RM, EDF } RtPolicy;

/* Simulates one hyperperiod (all tasks released together at tick 0). Returns the tick of the first missed deadline, or -1. */
static int rt_first_miss(RtPolicy pol, const Task *tk, int n, int horizon)
{
    int rem[MAXTASK] = {0}, deadline[MAXTASK] = {0};
    for (int t = 0; t < horizon; ++t) {
        for (int i = 0; i < n; ++i) {
            if (t % tk[i].period == 0) {
                if (rem[i] > 0) return t;                     /* the previous job is still unfinished at its deadline */
                rem[i] = tk[i].wcet;
                deadline[i] = t + tk[i].period;
            }
        }
        int best = -1;
        for (int i = 0; i < n; ++i) {
            if (rem[i] == 0) continue;
            if (best < 0) { best = i; continue; }
            if (pol == RM ? tk[i].period < tk[best].period : deadline[i] < deadline[best]) best = i;
        }
        if (best >= 0) --rem[best];
    }
    for (int i = 0; i < n; ++i)
        if (rem[i] > 0) return horizon;                       /* a job released in the last period did not finish */
    return -1;
}
#endif
```

```c
#include <math.h>
#include <stdio.h>
#include "rt.h"

static void show(const char *name, const Task *tk, int n)
{
    double u = 0;
    int hyper = 1;
    for (int i = 0; i < n; ++i) {
        u += (double)tk[i].wcet / tk[i].period;
        int a = hyper, b = tk[i].period;
        while (b) { int t = a % b; a = b; b = t; }
        hyper = hyper / a * tk[i].period;
    }
    double bound = n * (pow(2.0, 1.0 / n) - 1.0);
    int rm = rt_first_miss(RM, tk, n, hyper), edf = rt_first_miss(EDF, tk, n, hyper);
    printf("%-34s U = %.3f, RM bound %.3f, hyperperiod %2d: RM %s, EDF %s\n", name, u, bound, hyper,
           rm < 0 ? "meets all deadlines" : "misses a deadline", edf < 0 ? "meets all deadlines" : "misses a deadline");
    if (rm >= 0) printf("%34s RM first miss at tick %d\n", "", rm);
}

int main(void)
{
    Task a[2] = {{2, 5}, {4, 7}};
    Task b[3] = {{1, 2}, {1, 4}, {1, 8}};
    Task c[3] = {{1, 4}, {2, 6}, {3, 13}};
    Task d[2] = {{3, 4}, {2, 5}};
    show("(C=2,T=5) (C=4,T=7)", a, 2);
    show("(1,2) (1,4) (1,8) harmonic", b, 3);
    show("(1,4) (2,6) (3,13)", c, 3);
    show("(3,4) (2,5) overloaded", d, 2);
    return 0;
}
```

```text
(C=2,T=5) (C=4,T=7)                U = 0.971, RM bound 0.828, hyperperiod 35: RM misses a deadline, EDF meets all deadlines
                                   RM first miss at tick 7
(1,2) (1,4) (1,8) harmonic         U = 0.875, RM bound 0.780, hyperperiod  8: RM meets all deadlines, EDF meets all deadlines
(1,4) (2,6) (3,13)                 U = 0.814, RM bound 0.780, hyperperiod 156: RM meets all deadlines, EDF meets all deadlines
(3,4) (2,5) overloaded             U = 1.150, RM bound 0.828, hyperperiod 20: RM misses a deadline, EDF misses a deadline
                                   RM first miss at tick 5
```

Reading the four sets:

- `(C=2, T=5)` and `(C=4, T=7)` have `U = 0.971`, above the RM bound (0.828) and below 1: **EDF meets every deadline, RM misses one at tick 7.** This is the case for EDF.
- `(1,2) (1,4) (1,8)` have `U = 0.875`, above the bound (0.780) and **RM still works**: the periods are harmonic (each divides the next), where RM reaches `U = 1`. The bound is sufficient, not necessary.
- `(1,4) (2,6) (3,13)` have `U = 0.814`, above the bound again, and RM works: the bound says "maybe".
- `(3,4) (2,5)` has `U = 1.15`: nothing can work; both miss.

**Testing it.** `rt.h` was compared with exact oracles on 20,000 random task sets: EDF by the exact condition `U <= 1` (computed in integers), RM by response-time analysis (iterate `R = C_i + sum over higher-priority j of ceil(R / T_j) * C_j` until it settles or exceeds the period). They agree on every set. The set contained 10,160 sets both schedule, 424 that EDF schedules and RM does not, and 9,416 that neither does, so the comparison does exercise the case where the algorithms differ. Of six mutants, five were caught and the sixth is equivalent (above).

Linux offers all three levels: ordinary policies, `SCHED_FIFO` and `SCHED_RR` for fixed-priority real-time (section 9), and `SCHED_DEADLINE`, which the man page describes as using global EDF with a constant-bandwidth server (a task declares runtime, deadline and period) and ranks above every other policy. I did not run `SCHED_DEADLINE` here.

---

## 11. Multiprocessor scheduling

With several CPUs there are two designs: one **global** run queue that every CPU takes from, or a **per-CPU** queue with the scheduler assigning each job to a CPU. A global queue balances perfectly and becomes a lock contention point as the CPU count grows; per-CPU queues scale and can become unbalanced, which **load balancing** (migrating jobs) or **work stealing** (an idle CPU takes work from a busy one) repairs. Six jobs of 9, 1, 9, 1, 9, 1 ticks on 2 CPUs:

```c
#include <stdio.h>

/* Two CPUs, six jobs. Compare one global queue with per-CPU queues (static assignment, then with stealing). */
int main(void)
{
    int burst[6] = {9, 1, 9, 1, 9, 1}, n = 6;
    /* global queue: when a CPU is free it takes the next job in order */
    int free_at[2] = {0, 0};
    for (int i = 0; i < n; ++i) {
        int c = free_at[0] <= free_at[1] ? 0 : 1;
        free_at[c] += burst[i];
    }
    printf("global queue          : CPU 0 busy until %2d, CPU 1 busy until %2d, makespan %2d\n", free_at[0], free_at[1], free_at[0] > free_at[1] ? free_at[0] : free_at[1]);
    /* per-CPU queues, jobs dealt out alternately (job i goes to CPU i % 2) */
    int load[2] = {0, 0};
    for (int i = 0; i < n; ++i)
        load[i % 2] += burst[i];
    printf("per-CPU, no balancing : CPU 0 busy until %2d, CPU 1 busy until %2d, makespan %2d\n", load[0], load[1], load[0] > load[1] ? load[0] : load[1]);
    /* per-CPU queues with stealing: an idle CPU takes a job that has not started from the end of the other queue */
    int q[2][6], qn[2] = {0, 0}, head[2] = {0, 0}, t_free[2] = {0, 0};
    for (int i = 0; i < n; ++i)
        q[i % 2][qn[i % 2]++] = burst[i];
    int steals = 0, remaining = n;
    while (remaining) {
        int c = t_free[0] <= t_free[1] ? 0 : 1;
        if (head[c] < qn[c]) {
            t_free[c] += q[c][head[c]++];
        } else if (head[1 - c] < qn[1 - c]) {
            t_free[c] += q[1 - c][--qn[1 - c]];             /* steal from the tail of the busy CPU's queue */
            ++steals;
        } else {
            break;
        }
        --remaining;
    }
    printf("per-CPU with stealing : CPU 0 busy until %2d, CPU 1 busy until %2d, makespan %2d (%d steals)\n", t_free[0], t_free[1], t_free[0] > t_free[1] ? t_free[0] : t_free[1], steals);
    printf("lower bound (total work 30 / 2 CPUs) = 15\n");
    return 0;
}
```

```text
global queue          : CPU 0 busy until 19, CPU 1 busy until 11, makespan 19
per-CPU, no balancing : CPU 0 busy until 27, CPU 1 busy until  3, makespan 27
per-CPU with stealing : CPU 0 busy until 18, CPU 1 busy until 12, makespan 18 (1 steals)
lower bound (total work 30 / 2 CPUs) = 15
```

- The global queue finished at tick 19 (the best a list-order dispatch could do).
- Per-CPU queues with jobs dealt alternately (all three long jobs landed on CPU 0) finished at tick 27 while CPU 1 sat idle after tick 3.
- **Stealing** fixed most of it: one steal brought the makespan to 18, close to the lower bound of 15 (total work 30 divided by 2).

**Affinity.** A migrated thread finds its data in the wrong CPU's cache (this tutorial cannot show that without hardware counters; [CO 13](../04_computer_organization/13_cache_basics.md) covers why), so schedulers prefer to keep a thread on the CPU it last used (**soft affinity**) and let the programmer pin it (**hard affinity**, `sched_setaffinity`). The program below runs two equal CPU-bound threads either both pinned to CPU 0 or one per CPU:

```c
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <time.h>

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static void *work(void *arg)
{
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET((int)(long)arg, &s);
    sched_setaffinity(0, sizeof s, &s);
    volatile unsigned long x = 0;
    for (long i = 0; i < 400000000L; ++i)
        x += i;
    return NULL;
}

static double run(int cpu_a, int cpu_b)
{
    pthread_t a, b;
    double t0 = now_s();
    pthread_create(&a, NULL, work, (void *)(long)cpu_a);
    pthread_create(&b, NULL, work, (void *)(long)cpu_b);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    return now_s() - t0;
}

int main(void)
{
    double one = 1e18, two = 1e18;
    for (int r = 0; r < 3; ++r) {
        double a = run(0, 0), b = run(0, 1);
        if (a < one) one = a;
        if (b < two) two = b;
    }
    printf("two equal CPU-bound threads, both pinned to CPU 0: %.2f s\n", one);
    printf("two equal CPU-bound threads, one per CPU:          %.2f s\n", two);
    printf("ratio: %.2f\n", one / two);
    return 0;
}
```

```text
two equal CPU-bound threads, both pinned to CPU 0: 1.55 s
two equal CPU-bound threads, one per CPU:          0.75 s
ratio: 2.05
```

Two threads on one CPU took about twice as long (ratio 2.05): the CPU is the resource and pinning leaves the other one idle. Used well, pinning avoids migrations; used badly, it wastes a CPU. How Linux balances load across CPUs, NUMA nodes and cache domains is OS 05 and OS 13.

---

## What it costs

| Policy or mechanism | Cost or effect | Where measured |
|---|---|---|
| FCFS | Convoy effect: the short job C waited 7 ticks | Section 4 |
| SRTF | Lowest average waiting (3.00 against 4.75 for FCFS) but needs the future | Section 4 |
| Round robin, small quantum | Many dispatches: 42 at q=1 for 70 ticks of work | Section 5 |
| Starvation under priority | Victim waited 22 ticks; 12 with aging | Section 6 |
| MLFQ | Response 0.00, turnaround 9.50 on a CPU-bound workload | Section 4 |
| Lottery, short run | 7 / 26 / 67 percent against 10 / 20 / 70 ideal | Section 7 |
| Nice 10 against nice 0 | 9.1 to 9.3 times less work | Section 9 |
| Real-time hog against a normal process | Normal process keeps about 5 percent | Section 9 |
| Two threads on one CPU | Twice the elapsed time | Section 11 |

---

## Common mistakes

1. **Mixing up the metrics.** Waiting time excludes the burst; turnaround includes it; response is the time to first run.
2. **Counting the preempted job before a simultaneous arrival (or the reverse) without saying so.** The round-robin tie rule changes the answer (Problem B).
3. **Calling SJF practical.** It needs burst lengths that are not known; real systems estimate.
4. **Forgetting that SRTF can starve long jobs.** A stream of short ones postpones them indefinitely.
5. **Choosing the quantum by response time only.** Too small spends the CPU on dispatches (66.4 against 36.0 here at q=1 with a 1-tick cost).
6. **Assuming round robin lowers turnaround.** For jobs of unequal length it usually raises it; it lowers response time.
7. **Thinking priority numbers always mean "larger is more important".** Unix nice values run from -20 (most important) to 19; Linux real-time priorities run 1 to 99 with 99 highest. Read the convention first.
8. **Treating nice as a strict priority.** On Linux it is a weight: nice 10 against nice 0 still gets 10 percent of the CPU, not zero.
9. **Believing a real-time process can take 100 percent of a Linux CPU.** The throttle reserves 5 percent for normal tasks by default.
10. **Believing `U <= 0.693` is necessary for RM.** It is a sufficient bound; harmonic periods reach 1.0.
11. **Trusting a short benchmark window on a scheduler experiment.** A 3-second window gave 27 percent where the steady state is 5 percent.
12. **Forgetting `fflush` before `_exit` in a measuring child.** The first version of the real-time demo printed nothing because the output was lost in the buffer (the same trap as OS 02).
13. **Believing MLFQ always favours short jobs.** It favours jobs that give up the CPU early; a long CPU-bound job gets the worst treatment, and gaming it needs rule 4 to be accounted by total time, not by quantum.
14. **Reading `sched_rr_get_interval` on a normal process as a quantum.** It returned 0 here.

---

## Questions out loud

1. **What does a CPU scheduler do, and when does it run?** It picks the next ready thread. It runs when the current one blocks or exits, when a thread becomes ready, on a timer tick, and when priorities change.
2. **Preemptive versus non-preemptive?** Whether the OS may take the CPU from a running job before it gives it up. Preemptive is needed for response time and fairness.
3. **Define turnaround, waiting and response time.** `f - a`, `f - a - b`, `s - a`.
4. **What is the convoy effect?** Short jobs queue behind one long job under FCFS; waiting times balloon (average waiting 17 against 3 in the example of section 4.1).
5. **Why is SRTF optimal for average waiting time, and why is it not used as is?** An exchange argument shows it; it needs future burst lengths, which are estimated by exponential averaging, and it can starve long jobs.
6. **Round robin: what does the quantum trade off?** Response time against switching overhead; large q approaches FCFS, tiny q wastes the CPU on dispatches.
7. **How do you choose a quantum?** Larger than most CPU bursts (about 80 percent of them), small enough for response. Typically 10 to 100 ms.
8. **What is starvation? How do you prevent it?** A ready job never runs. Aging raises the priority of waiting jobs; MLFQ boosts; fair-share schedulers guarantee a share.
9. **What is priority inversion?** A high-priority job waits for a lock held by a low-priority one that cannot run because a medium one does. Priority inheritance fixes it.
10. **State the MLFQ rules.** The five rules of section 8 and what each fixes.
11. **How does MLFQ approximate SJF?** New jobs start at the top, so short jobs finish there; jobs that use their allotment fall; so long CPU-bound jobs end up at the bottom, behind the short ones.
12. **Lottery versus stride?** Both proportional share; lottery is random and noisy over short runs, stride is deterministic and exact, with a harder arrival rule.
13. **RM versus EDF?** RM: fixed priority by period, sufficient bound `n(2^(1/n) - 1)`, exact test by response-time analysis. EDF: dynamic priority by deadline, schedulable iff `U <= 1` for implicit deadlines.
14. **Global versus per-CPU run queues?** Global balances automatically and contends on a lock; per-CPU scales and needs load balancing or stealing.
15. **What does `nice` do on Linux?** It sets a weight, about 1.25 times per step; measured 9.1 to 1 at nice 10.
16. **How can a real-time task starve the system, and what stops it?** A `SCHED_FIFO` loop never yields; the kernel throttles real-time tasks to 95 percent per second so normal tasks keep 5.
17. **What is HRRN, and why does it not starve long jobs?** It runs the job with the highest `(waiting + burst) / burst`. A waiting job's ratio keeps rising, so in section 4.5 the long job finished at 11 instead of 21 under SJF.

---

## Problems

Solve each on paper first, then run the program.

**Where to solve them.** A, B and D need only the simulator headers (`sim.h`) and a C compiler. C needs `rt.h`. E needs `sim.h`. Everything compiles on any Linux or macOS machine with a C compiler; nothing here needs root. The Linux experiments of sections 9 and 11 need Linux, and the real-time ones need root.

**How to approach them.** For each scheduling problem draw the Gantt chart before computing a metric, and state the tie rule. For C compute the utilisation first, then test the bound, then simulate. For D, tabulate the trade-off before choosing. For E, decide what the boost does to the queue order before coding.

### Problem A (E, entry level): the four classic algorithms

Four jobs: A arrives at 0 with burst 10, B at 1 with burst 2, C at 2 with burst 1, D at 3 with burst 3. Compute the finish times and the average turnaround, waiting and response time under FCFS, SJF, SRTF and round robin with quantum 3.

### Problem B (E, entry level): round robin and the tie rule

A arrives at 0 with burst 5, B at 1 with burst 3, C at 4 with burst 2. Round robin, quantum 2. Write the run order tick by tick and the finish times, with the convention that a job arriving at the instant a quantum expires is queued before the preempted job.

### Problem C (M, medium): schedulable or not?

For each task set, `(C, T)` with deadline equal to the period: set 1 `(1,3) (1,4) (2,6)`; set 2 `(3,6) (4,8)`; set 3 `(2,4) (2,6) (2,9)`. Compute `U`, say whether RM and EDF meet every deadline, and where RM first misses.

### Problem D (M, medium): pick the quantum

Five jobs of 4, 7, 10, 13 and 16 ticks all arrive at 0. A dispatch costs 2 ticks. Find the quantum that minimises average turnaround subject to an average response time of at most 10 ticks.

### Problem E (H, hard): the priority boost

Add a priority boost to the MLFQ of `sim.h` (every `B` ticks every unfinished job returns to the top queue). Using a long job and a stream of short jobs, show how much more service the long job gets, and explain why its finish time need not improve.

---

## Answers

**A.** The program:

```c
#include <stdio.h>
#include "sim.h"

/* Problem A: waiting, turnaround and response times for four jobs under four policies. */
static void show(const char *name, Policy pol, int q, const Proc *p, int n)
{
    static Result r;
    sched_run(pol, q, 0, p, n, &r);
    double tat = 0, wait = 0, resp = 0;
    printf("%-14s finish:", name);
    for (int i = 0; i < n; ++i) {
        printf(" %c=%2d", 'A' + i, r.finish[i]);
        tat += r.finish[i] - p[i].arrival;
        wait += r.finish[i] - p[i].arrival - p[i].burst;
        resp += r.first[i] - p[i].arrival;
    }
    printf("   avg turnaround %5.2f  waiting %5.2f  response %5.2f\n", tat / n, wait / n, resp / n);
}

int main(void)
{
    Proc p[4] = {{0, 10, 0}, {1, 2, 0}, {2, 1, 0}, {3, 3, 0}};
    printf("A(arrive 0, burst 10) B(1, 2) C(2, 1) D(3, 3)\n");
    show("FCFS", FCFS, 0, p, 4);
    show("SJF", SJF, 0, p, 4);
    show("SRTF", SRTF, 0, p, 4);
    show("RR, quantum 3", RR, 3, p, 4);
    return 0;
}
```

```text
A(arrive 0, burst 10) B(1, 2) C(2, 1) D(3, 3)
FCFS           finish: A=10 B=12 C=13 D=16   avg turnaround 11.25  waiting  7.25  response  7.25
SJF            finish: A=10 B=13 C=11 D=16   avg turnaround 11.00  waiting  7.00  response  7.00
SRTF           finish: A=16 B= 3 C= 4 D= 7   avg turnaround  6.00  waiting  2.00  response  0.50
RR, quantum 3  finish: A=16 B= 5 C= 6 D= 9   avg turnaround  7.50  waiting  3.50  response  2.00
```

Check FCFS by hand: A runs 0 to 10, B 10 to 12, C 12 to 13, D 13 to 16. Waiting times 0, 9, 10, 10: average 7.25. SJF: A is alone at tick 0 and runs to 10 (no preemption); then C (1), B (2), D (3): finishes 11, 13, 16 for C, B, D: waiting 0, 10, 8, 10: average 7.00. SRTF: B arrives at 1 with 2 against A's 9 left and takes the CPU; C arrives at 2 with 1 against B's 1 left (a tie, B arrived first and keeps it) and runs after B; D follows; A finishes last at 16: average waiting 2.00. The convoy effect is stark: FCFS averages 7.25 against SRTF's 2.00 because the long job went first.

**B.** The run order from the simulator:

```text
A(0, burst 5) B(1, burst 3) C(4, burst 2), quantum 2
run order per tick: AABBAACCBA
finish: A=10 B=9 C=8
```

A runs 0 to 2 (its quantum), B 2 to 4, then at tick 4 B's quantum expires as C arrives. With C queued before B the queue is A, C, B: A 4 to 6, C 6 to 8, B 8 to 9, A 9 to 10. If the other convention were used (preempted job first) the queue would be A, B, C and the finish times would change (B would finish before C).

**C.** The program and results:

```c
#include <stdio.h>
#include "rt.h"

/* Problem C: are these task sets schedulable under RM and under EDF? */
static void show(const char *name, const Task *tk, int n)
{
    double u = 0;
    int hyper = 1;
    for (int i = 0; i < n; ++i) {
        u += (double)tk[i].wcet / tk[i].period;
        int a = hyper, b = tk[i].period;
        while (b) { int t = a % b; a = b; b = t; }
        hyper = hyper / a * tk[i].period;
    }
    int rm = rt_first_miss(RM, tk, n, hyper), edf = rt_first_miss(EDF, tk, n, hyper);
    printf("%s: U = %.3f; RM %s; EDF %s\n", name, u, rm < 0 ? "meets every deadline" : "misses a deadline", edf < 0 ? "meets every deadline" : "misses a deadline");
    if (rm >= 0) printf("   RM first miss at tick %d\n", rm);
}

int main(void)
{
    Task s1[3] = {{1, 3}, {1, 4}, {2, 6}};
    Task s2[2] = {{3, 6}, {4, 8}};
    Task s3[3] = {{2, 4}, {2, 6}, {2, 9}};
    show("set 1 (1,3) (1,4) (2,6)", s1, 3);
    show("set 2 (3,6) (4,8)", s2, 2);
    show("set 3 (2,4) (2,6) (2,9)", s3, 3);
    return 0;
}
```

```text
set 1 (1,3) (1,4) (2,6): U = 0.917; RM meets every deadline; EDF meets every deadline
set 2 (3,6) (4,8): U = 1.000; RM misses a deadline; EDF meets every deadline
   RM first miss at tick 8
set 3 (2,4) (2,6) (2,9): U = 1.056; RM misses a deadline; EDF misses a deadline
   RM first miss at tick 9
```

Set 1 has `U = 0.917`, above the RM bound for 3 tasks (0.780) but both algorithms meet every deadline in the simulation. Set 2 has `U = 1.000`: EDF is exactly at its limit and works; RM misses at tick 8. Set 3 has `U = 1.056 > 1`: neither can work.

**D.** The program prints the trade-off:

```c
#include <stdio.h>
#include "sim.h"

/* Problem D: lowest average turnaround with an average response of at most 10 ticks, when a dispatch costs 2 ticks. */
int main(void)
{
    Proc p[5] = {{0, 4, 0}, {0, 7, 0}, {0, 10, 0}, {0, 13, 0}, {0, 16, 0}};
    int cost = 2, best_q = 0;
    double best = 1e18;
    static Result r;
    printf("quantum  dispatches  avg response  avg turnaround (both including the switch cost)\n");
    for (int q = 1; q <= 20; ++q) {
        sched_run(RR, q, 0, p, 5, &r);
        int disp = 0, at[MAXT];
        for (int t = 0; t < r.len; ++t) {
            if (r.run[t] != IDLE && (t == 0 || r.run[t] != r.run[t - 1])) ++disp;
            at[t] = disp;
        }
        double tat = 0, resp = 0;
        for (int i = 0; i < 5; ++i) {
            tat += r.finish[i] + cost * at[r.finish[i] - 1];
            resp += r.first[i] + cost * at[r.first[i]];
        }
        tat /= 5;
        resp /= 5;
        if (q <= 6 || q == 8 || q == 10 || q == 13 || q == 16 || q == 20)
            printf("%7d  %10d  %12.1f  %14.1f\n", q, disp, resp, tat);
        if (resp <= 10.0 && tat < best) { best = tat; best_q = q; }
    }
    printf("best quantum with average response <= 10: %d (average turnaround %.1f)\n", best_q, best);
    return 0;
}
```

```text
quantum  dispatches  avg response  avg turnaround (both including the switch cost)
      1          47           8.0           106.8
      2          25          10.0            70.4
      3          19          12.0            63.6
      4          14          14.0            49.6
      5          11          15.2            45.0
      6          11          16.4            46.4
      8           8          18.2            38.4
     10           7          19.4            33.6
     13           5          20.0            30.0
     16           5          20.0            30.0
     20           5          20.0            30.0
best quantum with average response <= 10: 2 (average turnaround 70.4)
```

Average response is 8.0 at q=1, 10.0 at q=2 and 12.0 at q=3, so the largest quantum meeting the response limit is 2; its turnaround with switch cost is 70.4. Larger quanta have much lower turnaround (30.0 at q=13) but a response of 20, which breaks the limit. The response constraint, not the switch cost, forces the answer.

**E.** The program of section 8, with output:

```text
boost off agrees with the simulator for all 16 jobs: yes
ticks of service the long job (burst 12) has had by tick 30: 2 with no boost, 6 with a boost every 10 ticks
long job finishes at tick 42 with no boost, 42 with a boost
the last short job finishes at tick 32 with no boost, 36 with a boost
```

The boost moves every job, in queue order, back to level 0, so the long job gets one more quantum of service in each boost period (2 ticks of service by tick 30 become 6). The short jobs wait longer for it (the last one finishes at 36 instead of 32). The long job's finish time stays at 42 because the CPU is never idle (total work is 42 ticks) and it is the last job to finish in both cases; what the boost changes is how much service it has had *along the way*, which matters when it is a user's job waiting, not when you only watch the end.

---

## Cheat sheet

| Algorithm | Preemptive? | Strength | Weakness |
|---|---|---|---|
| FCFS | No | Simple; no starvation | Convoy effect |
| SJF | No | Optimal average waiting (non-preemptive) | Needs burst lengths; starves long jobs |
| SRTF | Yes | Optimal average waiting overall | Same |
| Round robin | Yes | Bounded response; fair | Overhead; poor turnaround |
| Priority | Either | Expresses importance | Starvation; inversion |
| MLFQ | Yes | Learns job type; good response | Tuning; gaming without accounting |
| HRRN | No | SJF-like, with built-in aging | Needs burst lengths |
| Multilevel queue | Either | Separate policy per class | Fixed classes; the lower class can starve |
| Lottery / stride | Yes | Proportional share | Noisy short run / arrival rule |
| RM | Fixed priority | Predictable | `U` bound `n(2^(1/n) - 1)` sufficient only |
| EDF | Dynamic priority | `U <= 1` | Overload behaves badly (from memory) |

| Metric | Formula |
|---|---|
| Turnaround | `finish - arrival` |
| Waiting | `finish - arrival - burst` |
| Response | `first run - arrival` |
| Linux numbers here | nice 10 gives 9.1:1, FIFO hog leaves 5.2 percent, RR quantum 100 ms, 2 threads on 1 CPU take 2x |

---

## Sources

- [sched(7), Linux man pages](https://man7.org/linux/man-pages/man7/sched.7.html): policies, priority ranges, `SCHED_FIFO` and `SCHED_RR`, nice, `SCHED_DEADLINE`, real-time throttling defaults (fetched).
- [EEVDF scheduler, kernel documentation](https://www.kernel.org/doc/html/latest/scheduler/sched-eevdf.html): lag, eligibility, virtual deadline; replaced CFS in Linux 6.6 (fetched).
- [OSTEP: Multi-level Feedback Queue](https://pages.cs.wisc.edu/~remzi/OSTEP/cpu-sched-mlfq.pdf): the rules, starvation, gaming, the boost (fetched).
- [OSTEP: Scheduling: Proportional Share](https://pages.cs.wisc.edu/~remzi/OSTEP/cpu-sched-lottery.pdf): lottery and stride scheduling (fetched).
- [OS 02](02_processes.md) and [OS 03](03_threads.md): the process states and the cost of switching.
- Marked "from memory" in the text and not fetched or measured: the exchange argument for SRTF, the exponential-average burst estimate, the 80 percent rule for the quantum, priority inversion and inheritance, the Liu and Layland bounds, the nice weight table, and the claim that EDF degrades badly in overload.
