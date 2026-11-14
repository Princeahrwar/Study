# OS 21: The OS question bank and numericals

*Part of the [Operating systems plan](00_plan.md) · Time: about 12 hours (4 reading, 8 drills against a clock) · You need: the OS track so far. Numerical drills lean on [OS 04](04_cpu_scheduling_algorithms.md) (scheduling), [OS 09](09_deadlocks.md) (deadlock), [OS 10](10_memory_management.md), [OS 11](11_virtual_memory.md) and [OS 12](12_virtual_memory_and_tlb.md) (paging, translation, the TLB), [OS 14](14_io_systems_and_disk_scheduling.md) (disk scheduling), [OS 15](15_file_systems.md) and [OS 16](16_crash_consistency_and_the_page_cache.md) (inodes, the page cache) and [OS 07](07_condition_variables_and_semaphores.md) (semaphores). The walkthroughs lean on [OS 02](02_processes.md), [OS 19](19_user_kernel_boundary.md) and [OS 20](20_protection_and_security.md).*

This is the last tutorial of the OS track, and it is a different kind of document: it teaches nothing new. It is a **drill book**. Two kinds of OS interview question remain after the concepts are learned, and both are about speed and accuracy. The first is the **numerical**: a workload, a reference string or a queue of cylinders, and a number that must come out right in a few minutes (average waiting time, page faults, total head movement, whether a state is safe, the largest file an inode can address). The second is the **walkthrough**: "what happens when you run `./a.out`", "what does `read()` do when the data is not cached", "what does Ctrl-C do", where the interviewer wants the chain of mechanisms in the right order, with the right names. A third kind, the **short conceptual question**, is the one that fills the first ten minutes of most loops. This tutorial has all three: a solver program for each numerical type (so every worked answer was **computed by a program and not by hand**), three walkthroughs checked against a running kernel, and a bank of 66 conceptual questions with the two-sentence answer each should get.

**Four honest limits, stated first.** (1) The numerical answers follow **one set of conventions**, and textbooks differ. Where they differ the program's choice is stated (the tie rule in round robin, whether the return stroke of C-SCAN counts, what the TLB lookup costs on a miss), because on an exam the answer that matches the stated convention is the right one. (2) The solvers were checked against the standard textbook instances (FIFO 15, LRU 12 and OPT 9 faults on the 20-page string, Belady's 9 and 10, the 640-cylinder FCFS total, the Banker's example), which I know from memory of the usual texts; I did not look up the books during this session. (3) The walkthroughs are measured on **one machine**: a Firecracker micro-VM running Linux 6.18.44 as root, so counts of system calls and timings are this machine's, and the ordering of events is the part to remember. (4) The conceptual answers are **summaries of the earlier tutorials and of standard material**; where a fact was measured in a tutorial I say which, and everything else is from memory. Anything I did not run or fetch is marked "from memory".

By the end you can:

- compute CPU-scheduling averages for FCFS, SJF, SRTF, round robin and priority from a Gantt chart without error (computed: on the workload below FCFS gave an average wait of 8.75, SJF 7.75, SRTF 6.50 and round robin with quantum 3 gave 13.50, and SRTF was the best of the five on waiting time as theory says);
- run the Banker's safety algorithm and a request check by hand (computed: the starting state is safe with the sequence P1, P3, P0, P2, P4; a request by P1 for (1,0,2) is granted, a request by P4 for (3,3,0) is refused because nothing can finish afterwards);
- simulate FIFO, LRU, OPT and clock and spot Belady's anomaly (computed: 15, 12, 9 and 14 faults on the 20-page string with 3 frames; FIFO went from 9 to 10 faults when frames went from 3 to 4);
- size page tables and compute an effective access time with a TLB and with page faults (computed: a flat 32-bit table with 4 KiB pages and 4-byte entries is 4,194,304 bytes; with a TLB hit ratio of 0.98 the access takes 112 ns; one fault in 399,990 accesses is the most that a 10 percent slowdown allows when a fault costs 8 ms);
- order disk requests under six policies and total the head movement (computed: FCFS 640 cylinders, SSTF 236, SCAN 331, C-SCAN 382, LOOK 299, C-LOOK 322 for the classic queue with the head at 53 moving up);
- do file-size arithmetic for the classic inode, for FAT, and for what ext4 really allocates (computed: 12 direct plus single, double and triple indirect with 4 KiB blocks and 4-byte pointers addresses 4,402,345,721,856 bytes; measured: a 1-byte file occupied 4,096 bytes on disk and a 1 GiB sparse file occupied none);
- trace semaphores step by step, including a deadlock (computed: the producer that takes the mutex before it waits for a free slot deadlocked: the consumer blocked on the mutex and the producer on a free slot);
- narrate `./a.out` from `fork` to `exit` with measured counts (measured: an empty `main` made 30 system calls when dynamically linked, 15 when static), the `read()` miss path (measured: a cold 4 KiB read took about 200 microseconds and the warm repeat under 2) and the Ctrl-C path (measured: the foreground group was killed by signal 2 and the background job survived);
- answer 66 short conceptual questions crisply and know which tutorial holds the evidence.

**Where the numbers come from.** Eleven programs were compiled with GCC 13.3 (`-O2 -Wall -Wextra`) and run on the machine described above (glibc 2.39, x86_64, an Intel Xeon at a nominal 2.10 GHz). Seven are pure solvers (`q1` to `q7`, deterministic, no timing) and four are measurement programs for the walkthroughs (`q8a` for the program image, `q8a_empty` for the system-call count, `q8b` for the page cache, `q8c` for the terminal). The solvers print their steps so you can compare your working with theirs. Kernel and library behaviour was checked against the manual pages `execve(2)`, `getauxval(3)`, `posix_fadvise(2)`, `mincore(2)` and `termios(3)`, listed under Sources with what each did and did not confirm.

**Data and timing.** The solvers are deterministic: the same input gives the same output on every machine, and a mismatch with your hand answer is a mistake in the hand answer or a difference of convention, never noise. The timings in the walkthroughs are medians or single runs on a VM and vary by 10 to 30 percent; process IDs and addresses differ every run, so the text quotes shapes and counts, not those.

---

## 0. How to use this tutorial

**Drill against a clock.** An interview or an exam gives a few minutes per numerical. These are the targets I would hold yourself to, from experience with the usual formats (from memory, not a measurement):

| Type | Target | The step that costs the most time |
| --- | --- | --- |
| CPU scheduling, four to five processes, one algorithm | 4 minutes | drawing the Gantt chart without losing a tick |
| Banker's safety test, 5 processes, 3 resources | 5 minutes | recomputing `need = max - allocation` correctly |
| Page replacement, 20 references, 3 frames | 4 minutes | LRU: keeping "last used" straight |
| Page-table size and EAT | 3 minutes | stating the formula and units before substituting |
| Disk scheduling, 8 requests | 3 minutes | SCAN and C-SCAN: whether the end of the disk counts |
| Inode and file size | 3 minutes | the number of pointers per block |
| Semaphore trace | 5 minutes | remembering that a `V` wakes a waiter without raising the count |

**The method for every numerical.** (1) Write the **formula or definition** first, with units. (2) Write the **conventions** you are assuming in one line (the tie rule, whether the return stroke counts). A stated convention is half the marks and all of the defence in an interview. (3) Do the computation **visibly** (Gantt chart, frame table, queue). (4) **Sanity-check** with a bound: the average wait of FCFS and SJF must differ by the order of magnitude you expect; OPT must never fault more than LRU; a SCAN total must be at least the distance to the farthest request. (5) State the answer with units and say what it means.

**Check against the programs.** Each section prints the solver's output. Solve the instance by hand first, then compare. A difference tells you which convention you used.

---

## 1. CPU scheduling

### 1.1 Definitions to write first

- **Turnaround time** = completion time minus arrival time.
- **Waiting time** = turnaround time minus burst time (the time spent in the ready queue, not the time spent waiting for I/O).
- **Response time** = time of first run minus arrival time.
- **Average** = sum over processes divided by the number of processes. Throughput is processes completed per unit time.

### 1.2 The workload and the conventions

The solver simulates one tick at a time. The workload (arrival, burst, priority; a lower priority number is a higher priority):

```text
workload (arrival, burst, priority; lower number = higher priority)
  P1 arrives 0 burst 8 priority 3
  P2 arrives 1 burst 4 priority 1
  P3 arrives 2 burst 9 priority 4
  P4 arrives 3 burst 5 priority 2
```

**Conventions** the program applies, which you should state on an exam: SJF and priority are **non-preemptive** (the choice is made only when the CPU is free); SRTF is the **preemptive** version of SJF (the choice is remade at every tick, and a process arriving with a strictly shorter remaining time preempts); ties go to the lower arrival time and then the lower index; in round robin a process that **arrives at the same tick** as another's quantum expires is placed in the queue **before** the preempted process. The last rule is the one textbooks disagree on, and it changes the answer.

```c
static void run(int algo, int q, const char *title)
{
    int rem[N], done[N] = { 0 }, finish[N] = { 0 }, queue[64], qh = 0, qt = 0;
    char gantt[256];
    int t = 0, cur = -1, slice = 0, left = N, g = 0;
    for (int i = 0; i < N; i++) rem[i] = P[i].burst;
    while (left) {
        for (int i = 0; i < N; i++) if (P[i].arrive == t && algo == RR) { queue[qt++] = i; }
        if (algo == RR && cur >= 0 && slice == q && rem[cur] > 0) { queue[qt++] = cur; cur = -1; }
        if (algo == FCFS || algo == SJF || algo == PRIO) {
            if (cur < 0) {                                     /* non-preemptive: choose only when idle */
                int best = -1;
                for (int i = 0; i < N; i++) {
                    if (done[i] || P[i].arrive > t) continue;
                    if (best < 0) { best = i; continue; }
                    int a = algo == FCFS ? P[i].arrive : algo == SJF ? P[i].burst : P[i].prio;
                    int b = algo == FCFS ? P[best].arrive : algo == SJF ? P[best].burst : P[best].prio;
                    if (a < b) best = i;
                }
                cur = best;
            }
        } else if (algo == SRTF) {
            int best = -1;                                     /* preemptive: re-choose every tick */
            for (int i = 0; i < N; i++) {
                if (done[i] || P[i].arrive > t) continue;
                if (best < 0 || rem[i] < rem[best]) best = i;
            }
            cur = best;
        } else if (cur < 0 && qh < qt) { cur = queue[qh++]; slice = 0; }
        if (cur < 0) { gantt[g++] = '.'; t++; continue; }
        gantt[g++] = '1' + cur;
        rem[cur]--; slice++; t++;
        if (rem[cur] == 0) { done[cur] = 1; finish[cur] = t; left--; cur = -1; }
    }
    gantt[g] = 0;
    double w = 0, ta = 0;
    printf("%s\n  Gantt (one character per tick, digit = process): %s\n", title, gantt);
    for (int i = 0; i < N; i++) {
        int tat = finish[i] - P[i].arrive, wt = tat - P[i].burst;
        printf("  %s finish %2d turnaround %2d waiting %2d\n", P[i].name, finish[i], tat, wt);
        w += wt; ta += tat;
    }
    printf("  average waiting %.2f, average turnaround %.2f\n", w / N, ta / N);
}
```

### 1.3 The five answers

```text
FCFS
  Gantt (one character per tick, digit = process): 11111111222233333333344444
  P1 finish  8 turnaround  8 waiting  0
  P2 finish 12 turnaround 11 waiting  7
  P3 finish 21 turnaround 19 waiting 10
  P4 finish 26 turnaround 23 waiting 18
  average waiting 8.75, average turnaround 15.25
```

```text
SJF (non-preemptive)
  Gantt (one character per tick, digit = process): 11111111222244444333333333
  P1 finish  8 turnaround  8 waiting  0
  P2 finish 12 turnaround 11 waiting  7
  P3 finish 26 turnaround 24 waiting 15
  P4 finish 17 turnaround 14 waiting  9
  average waiting 7.75, average turnaround 14.25
SRTF (preemptive SJF)
  Gantt (one character per tick, digit = process): 12222444441111111333333333
  P1 finish 17 turnaround 17 waiting  9
  P2 finish  5 turnaround  4 waiting  0
  P3 finish 26 turnaround 24 waiting 15
  P4 finish 10 turnaround  7 waiting  2
  average waiting 6.50, average turnaround 13.00
```

```text
Round robin, quantum 3
  Gantt (one character per tick, digit = process): 11122233344411123334411333
  P1 finish 23 turnaround 23 waiting 15
  P2 finish 16 turnaround 15 waiting 11
  P3 finish 26 turnaround 24 waiting 15
  P4 finish 21 turnaround 18 waiting 13
  average waiting 13.50, average turnaround 20.00
Priority (non-preemptive)
  Gantt (one character per tick, digit = process): 11111111222244444333333333
  P1 finish  8 turnaround  8 waiting  0
  P2 finish 12 turnaround 11 waiting  7
  P3 finish 26 turnaround 24 waiting 15
  P4 finish 17 turnaround 14 waiting  9
  average waiting 7.75, average turnaround 14.25
```

How to read it:

- **FCFS: average waiting 8.75.** P1 runs 0 to 8, P2 8 to 12 (waits 7), P3 12 to 21 (waits 10), P4 21 to 26 (waits 18). The long first job makes everyone wait: the **convoy effect**.
- **SJF non-preemptive: 7.75.** At time 8 the ready processes are P2 (4), P3 (9) and P4 (5), so P2, then P4, then P3. SJF minimises average waiting among **non-preemptive** schedules, and it needs the burst lengths in advance.
- **SRTF: 6.50, the best of the five.** At time 1 P2 arrives with 4 against P1's remaining 7, so it preempts; at time 3 P4 (5) arrives, but P2 has 2 left and keeps the CPU; P4 runs 5 to 10; P1 finishes 10 to 17; P3 last. SRTF is **provably optimal for average waiting time** (a result worth quoting), and its price is starvation of long jobs (P3 waits 15) and the need to predict bursts.
- **Round robin, quantum 3: 13.50.** The Gantt chart `111222333444111 2 333 44 11 333` shows the rule at work: at time 3 P4 arrives just as P1's quantum ends, so P4 is queued ahead of P1. Round robin has the **worst average waiting** here and the **best response time**: the first runs are at 0, 3, 6 and 9, so the response times (first run minus arrival) are 0, 2, 4 and 6, an average of 3.0, against 4.25 for SRTF (first runs at 0, 1, 17 and 5 give 0, 0, 15 and 2).
- **Priority non-preemptive: 7.75**, identical to SJF here because the priorities happen to order the jobs by burst length among those that are waiting: P2 (priority 1), P4 (2) and P3 (4) after P1. The identical Gantt chart is an accident of this data, and the sentence "priority equals SJF" is not general.

### 1.4 The traps

1. **Idle time at the start.** If no process has arrived, the CPU idles and the Gantt chart has a gap. The program draws it as `.`; the arithmetic of waiting time is unaffected.
2. **Waiting time is not "time since arrival"**: it is turnaround minus burst.
3. **Preemption points.** In SRTF a preemption happens only for a **strictly** shorter remaining time; with a tie the running process continues.
4. **Context-switch cost** is usually ignored in the exam and costs real time in practice. Count the switches in the Gantt chart if asked: round robin above switches 9 times against 3 for FCFS.
5. **Multi-level feedback queues and real-time schedulers** (rate-monotonic, earliest-deadline-first) are covered in [OS 04](04_cpu_scheduling_algorithms.md) and use their own tests: for rate-monotonic the utilisation bound `n(2^(1/n) - 1)`, about 0.693 for large n; for EDF, utilisation at most 1 (from memory of the standard results; the tutorial has the worked cases).

---

## 2. Deadlocks and the Banker's algorithm

### 2.1 Definitions to write first

A deadlock needs four conditions at once: **mutual exclusion, hold and wait, no preemption, circular wait**. The Banker's algorithm avoids it by **granting a request only if the resulting state is safe**. For each process, `need = max - allocation`, and `available = total - sum of allocations`. A state is **safe** if some order exists in which every process can obtain its maximum and finish. The **safety test**: set `work = available`; repeatedly find an unfinished process with `need <= work`, pretend it finishes (`work += its allocation`), and mark it done; the state is safe if all finish.

Unsafe does **not** mean deadlocked: an unsafe state is one from which a deadlock is possible if every process asks for its maximum.

### 2.2 The instance and the safety test

```c
static int safe(int alloc[NP][NR], int need[NP][NR], const int avail0[NR], int verbose)
{
    int work[NR], fin[NP] = { 0 }, count = 0;
    memcpy(work, avail0, sizeof work);
    if (verbose) printf("  work starts at (%d,%d,%d)\n", work[0], work[1], work[2]);
    while (count < NP) {
        int found = 0;
        for (int i = 0; i < NP; i++) {
            if (fin[i]) continue;
            int ok = 1;
            for (int r = 0; r < NR; r++) if (need[i][r] > work[r]) ok = 0;
            if (!ok) continue;
            for (int r = 0; r < NR; r++) work[r] += alloc[i][r];
            fin[i] = 1; count++; found = 1;
            if (verbose) printf("  P%d can finish (need <= work), releases its allocation: work becomes (%d,%d,%d)\n", i, work[0], work[1], work[2]);
            break;
        }
        if (!found) { if (verbose) printf("  no process can finish: UNSAFE\n"); return 0; }
    }
    return 1;
}

static void request(int p, const int req[NR])
{
    int alloc[NP][NR], need[NP][NR], avail[NR];
    memcpy(alloc, ALLOC, sizeof alloc);
    for (int i = 0; i < NP; i++) for (int r = 0; r < NR; r++) need[i][r] = MAX[i][r] - ALLOC[i][r];
    for (int r = 0; r < NR; r++) { avail[r] = TOTAL[r]; for (int i = 0; i < NP; i++) avail[r] -= ALLOC[i][r]; }
    printf("request by P%d for (%d,%d,%d)\n", p, req[0], req[1], req[2]);
    for (int r = 0; r < NR; r++) if (req[r] > need[p][r]) { printf("  exceeds the declared maximum: error\n"); return; }
    for (int r = 0; r < NR; r++) if (req[r] > avail[r]) { printf("  not available now: P%d must wait\n", p); return; }
    for (int r = 0; r < NR; r++) { avail[r] -= req[r]; alloc[p][r] += req[r]; need[p][r] -= req[r]; }
    printf("  pretend to grant it, then test safety:\n");
    printf("  %s\n", safe(alloc, need, avail, 1) ? "SAFE: grant it" : "UNSAFE: refuse, P must wait");
}
```

```text
total (10,5,7); allocation and maximum per process
  P0 alloc (0,1,0) max (7,5,3) need (7,4,3)
  P1 alloc (2,0,0) max (3,2,2) need (1,2,2)
  P2 alloc (3,0,2) max (9,0,2) need (6,0,0)
  P3 alloc (2,1,1) max (2,2,2) need (0,1,1)
  P4 alloc (0,0,2) max (4,3,3) need (4,3,1)
available (3,3,2)
safety test:
  work starts at (3,3,2)
  P1 can finish (need <= work), releases its allocation: work becomes (5,3,2)
  P3 can finish (need <= work), releases its allocation: work becomes (7,4,3)
  P0 can finish (need <= work), releases its allocation: work becomes (7,5,3)
  P2 can finish (need <= work), releases its allocation: work becomes (10,5,5)
  P4 can finish (need <= work), releases its allocation: work becomes (10,5,7)
  SAFE
```

How to read it:

- **The `need` matrix** is the first step and the source of most errors: P0's need is (7,4,3) because its maximum is (7,5,3) and it already holds (0,1,0).
- **Available** is `(10,5,7) - (7,2,5)` = **(3,3,2)**, the sum of the allocations being (0+2+3+2+0, 1+0+0+1+0, 0+0+2+1+2) = (7,2,5).
- **The safe sequence found is P1, P3, P0, P2, P4**; the answer is a set of valid sequences and the program reports the first one it finds in index order. A different valid order (for instance P1, P3, P4, P0, P2) is equally correct, and an exam should accept any, so check **yours** by running it: at every step `need <= work` must hold.
- **Work grows monotonically** as processes finish (3,3,2 up to 10,5,7), ending at the total: a built-in check. If it does not end at the total, an allocation was copied wrongly.

### 2.3 Resource requests

```text
request by P1 for (1,0,2)
  pretend to grant it, then test safety:
  work starts at (2,3,0)
  P1 can finish (need <= work), releases its allocation: work becomes (5,3,2)
  P3 can finish (need <= work), releases its allocation: work becomes (7,4,3)
  P0 can finish (need <= work), releases its allocation: work becomes (7,5,3)
  P2 can finish (need <= work), releases its allocation: work becomes (10,5,5)
  P4 can finish (need <= work), releases its allocation: work becomes (10,5,7)
  SAFE: grant it
request by P4 for (3,3,0)
  pretend to grant it, then test safety:
  work starts at (0,0,2)
  no process can finish: UNSAFE
  UNSAFE: refuse, P must wait
request by P0 for (0,2,0)
  pretend to grant it, then test safety:
  work starts at (3,1,2)
  P3 can finish (need <= work), releases its allocation: work becomes (5,2,3)
  P1 can finish (need <= work), releases its allocation: work becomes (7,2,3)
  P0 can finish (need <= work), releases its allocation: work becomes (7,5,3)
  P2 can finish (need <= work), releases its allocation: work becomes (10,5,5)
  P4 can finish (need <= work), releases its allocation: work becomes (10,5,7)
  SAFE: grant it
```

- **P1 requests (1,0,2): granted.** Checks in order: request at most need (yes, (1,2,2)), request at most available (yes, (3,3,2)), then pretend to grant and test safety. The pretended state has available (2,3,0) and is safe.
- **P4 requests (3,3,0): refused (it must wait).** It is within P4's need (4,3,1) and within the available (3,3,2), but granting it leaves available (0,0,2) and **no process can finish**: the state is unsafe, so the request is delayed even though the resources exist. This is the whole point of avoidance: refusing a request that is satisfiable now.
- **P0 requests (0,2,0): granted** (with available (3,1,2) afterwards, safe).

### 2.4 Detection, and what to say in an interview

Detection (rather than avoidance) uses the same algorithm with **current requests** in place of `need` and no maximum: a process whose request can be met finishes, and any process left over is deadlocked. For single-instance resources it reduces to **finding a cycle in the wait-for graph**. The question "why not use the Banker's algorithm in a real kernel" has a standard answer: it needs each process's maximum demand in advance, a fixed number of processes and resources, and it is costly (`O(m n^2)` for `m` resource types and `n` processes). Real systems **prevent** deadlock by lock ordering ([OS 09](09_deadlocks.md)) and **detect** it in special cases (the kernel's lockdep, database lock managers with timeouts and victim selection).

---

## 3. Page replacement

### 3.1 Definitions to write first

A **reference string** is the sequence of pages a process touches. With a fixed number of frames, a **fault** occurs when the page is not in a frame, and the algorithm picks a **victim** when no frame is free. The policies:

- **FIFO**: evict the page that has been in memory longest (ignores use).
- **LRU**: evict the page whose last use is oldest.
- **OPT** (Belady's optimal): evict the page whose next use is farthest in the future. Not implementable (needs the future); it is the **lower bound** against which others are judged.
- **Clock (second chance)**: a circular list with a reference bit; the hand skips pages with the bit set (clearing it) and evicts the first with the bit clear. It approximates LRU with one bit per page.

The first `frames` references to new pages are always faults (**cold misses**), so the minimum number of faults is the number of distinct pages.

### 3.2 The 20-page string with 3 frames

```c
static int simulate(int algo, const int *ref, int n, int frames, int show)
{
    int page[MAXF], last[MAXF], loaded[MAXF], refbit[MAXF], used = 0, faults = 0, hand = 0;
    for (int i = 0; i < n; i++) {
        int hit = -1;
        for (int f = 0; f < used; f++) if (page[f] == ref[i]) hit = f;
        if (hit >= 0) {
            last[hit] = i; refbit[hit] = 1;
        } else {
            faults++;
            int slot;
            if (used < frames) slot = used++;
            else if (algo == FIFO) { slot = 0; for (int f = 1; f < frames; f++) if (loaded[f] < loaded[slot]) slot = f; }
            else if (algo == LRU) { slot = 0; for (int f = 1; f < frames; f++) if (last[f] < last[slot]) slot = f; }
            else if (algo == OPT) {
                int far = -1; slot = 0;
                for (int f = 0; f < frames; f++) {
                    int nxt = n + 1;
                    for (int j = i + 1; j < n; j++) if (ref[j] == page[f]) { nxt = j; break; }
                    if (nxt > far) { far = nxt; slot = f; }
                }
            } else {
                while (refbit[hand]) { refbit[hand] = 0; hand = (hand + 1) % frames; }
                slot = hand; hand = (hand + 1) % frames;
            }
            page[slot] = ref[i]; last[slot] = i; loaded[slot] = i; refbit[slot] = 1;
        }
        if (show) {
            printf("  ref %d: ", ref[i]);
            for (int f = 0; f < frames; f++) { if (f < used) printf("%d ", page[f]); else printf("- "); }
            printf("%s\n", hit >= 0 ? "hit" : "FAULT");
        }
    }
    return faults;
}
```

```text
reference string of 20 pages, 3 frames
FIFO
  total faults 15 (fault rate 75%)
LRU
  ref 7: 7 - - FAULT
  ref 0: 7 0 - FAULT
  ref 1: 7 0 1 FAULT
  ref 2: 2 0 1 FAULT
  ref 0: 2 0 1 hit
  ref 3: 2 0 3 FAULT
  ref 0: 2 0 3 hit
  ref 4: 4 0 3 FAULT
  ref 2: 4 0 2 FAULT
  ref 3: 4 3 2 FAULT
  ref 0: 0 3 2 FAULT
  ref 3: 0 3 2 hit
  ref 2: 0 3 2 hit
  ref 1: 1 3 2 FAULT
  ref 2: 1 3 2 hit
  ref 0: 1 0 2 FAULT
  ref 1: 1 0 2 hit
  ref 7: 1 0 7 FAULT
  ref 0: 1 0 7 hit
  ref 1: 1 0 7 hit
  total faults 12 (fault rate 60%)
OPT
  total faults 9 (fault rate 45%)
Clock
  total faults 14 (fault rate 70%)
```

How to read it:

- **FIFO 15, LRU 12, OPT 9 faults** (75, 60 and 45 percent). This is the standard instance; if your hand count differs, recount the frames after each fault.
- **The LRU trace** shows the rule. At the tenth reference (page 3) the frames hold 4, 0 and 2, whose last uses were at the 8th, 7th and 9th references, so page 0 is the victim and the frames become `4 3 2`. Always compare **last-use positions**, never the order in which the pages sit in the frames.
- **Clock gave 14 faults**, between LRU and FIFO here. Clock's number depends on the initial hand and on whether a newly loaded page's reference bit starts at 1 (this program sets it to 1); a different convention gives a different count, so state it.
- **Sanity checks**: OPT's 9 is at most every other policy's count, and the string touches 6 distinct pages (7, 0, 1, 2, 3, 4), so 6 cold faults is the floor for any policy.

### 3.3 Belady's anomaly

```text
Belady's anomaly, string 1 2 3 4 1 2 5 1 2 3 4 5
  frames: 1 2 3 4 5 6
  FIFO  faults: 12 12 9 10 5 5
  LRU   faults: 12 12 10 8 5 5
  OPT   faults: 12 9 7 6 5 5
  Clock faults: 12 12 9 10 5 5
```

- **FIFO with 3 frames: 9 faults. With 4 frames: 10.** More memory made it **worse**. Belady's anomaly can occur for FIFO and not for **stack algorithms** (LRU and OPT), whose set of resident pages with `k` frames is always a subset of the set with `k + 1`. The columns for LRU (12, 12, 10, 8, 5, 5) and OPT (12, 9, 7, 6, 5, 5) fall or stay level as frames grow, and FIFO's (12, 12, 9, 10, 5, 5) does not.
- **Clock behaves like FIFO on this string** (the same row); clock is not a stack algorithm either.
- **The interview question**: "can adding memory increase page faults?" Yes for FIFO, this string is the example, and the reason is that FIFO's choice of victim ignores whether the page was recently used.

### 3.4 Traps

1. **Counting hits as faults or the reverse**, usually at the first references; count only references that find no frame holding the page.
2. **LRU victim by position in the frame list** instead of by last use.
3. **OPT ties**: when two pages are never used again, any may go; the fault count is the same.
4. **Working set and thrashing** are the concept questions that follow ([OS 11](11_virtual_memory.md)): the working set `W(t, delta)` is the set of pages referenced in the last `delta` references; if the sum of working sets exceeds memory, the system thrashes.

---

## 4. Address translation, page-table size and effective access time

### 4.1 Formulas to write first

- **Number of pages** = virtual address space / page size. **Offset bits** = log2(page size). **Page-number bits** = address bits minus offset bits.
- **Flat page-table size** = number of pages x entry size.
- **Multi-level**: if each table occupies one page, a table holds `page size / entry size` entries and consumes `log2(page size / entry size)` index bits; levels = ceiling of page-number bits divided by that.
- **Effective access time (EAT) with a TLB**, where `t` is the TLB lookup time, `m` the memory access time, `h` the hit ratio and `L` the number of levels: `EAT = h (t + m) + (1 - h)(t + L m + m)`. The lookup is paid on every access; a miss walks `L` table levels in memory and then makes the data access.
- **EAT with demand paging**, fault probability `p`: `EAT = (1 - p) m + p S`, where `S` is the fault service time.

State the model: the TLB lookup is **not** overlapped with the memory access, page tables are not cached, and the walk reads one memory word per level. Exams vary on these, and the program states its choice.

### 4.2 Sizes

```c
static void sizes(int va_bits, int page_bits, int pte_bytes)
{
    long long pages = 1LL << (va_bits - page_bits);
    printf("  %d-bit virtual address, %lld-byte pages, %d-byte entries: %lld entries, flat table %lld bytes (%.1f MiB)\n",
           va_bits, 1LL << page_bits, pte_bytes, pages, pages * pte_bytes, pages * pte_bytes / 1048576.0);
    int idx_bits = page_bits - (pte_bytes == 4 ? 2 : 3);       /* index bits per level when a table fills one page */
    int levels = (va_bits - page_bits + idx_bits - 1) / idx_bits;
    printf("    one table per page gives %d index bits per level, so %d levels: ", idx_bits, levels);
    int rest = va_bits - page_bits;
    for (int l = 0; l < levels; l++) { int b = rest > idx_bits ? idx_bits : rest; if (l == 0 && rest % idx_bits) b = rest % idx_bits; printf("%d ", b); rest -= b; }
    printf("+ offset %d\n", page_bits);
}
```

```text
page-table sizes
  32-bit virtual address, 4096-byte pages, 4-byte entries: 1048576 entries, flat table 4194304 bytes (4.0 MiB)
    one table per page gives 10 index bits per level, so 2 levels: 10 10 + offset 12
  48-bit virtual address, 4096-byte pages, 8-byte entries: 68719476736 entries, flat table 549755813888 bytes (524288.0 MiB)
    one table per page gives 9 index bits per level, so 4 levels: 9 9 9 9 + offset 12
  57-bit virtual address, 4096-byte pages, 8-byte entries: 35184372088832 entries, flat table 281474976710656 bytes (268435456.0 MiB)
    one table per page gives 9 index bits per level, so 5 levels: 9 9 9 9 9 + offset 12
  32-bit virtual address, 65536-byte pages, 4-byte entries: 65536 entries, flat table 262144 bytes (0.2 MiB)
    one table per page gives 14 index bits per level, so 2 levels: 2 14 + offset 16
```

- **32 bits, 4 KiB pages, 4-byte entries.** 2^20 pages, so 2^20 x 4 = 4,194,304 bytes (4 MiB) for a **flat** table per process. A 10/10/12 split makes each table exactly one page: a **two-level** table, and a process that uses little memory needs only the top table (4 KiB) plus the second-level tables it touches.
- **48 bits, 8-byte entries.** 2^36 entries x 8 = 512 GiB flat, which is absurd; 512 entries per page-sized table gives 9 index bits per level, so 36 / 9 = **4 levels** (the x86-64 layout). Printed as "524288.0 MiB", that is 512 GiB.
- **57 bits** needs 45 / 9 = **5 levels** (the layout behind x86-64 five-level paging; the program only does the arithmetic).
- **Bigger pages shrink the table**: 64 KiB pages with 4-byte entries leave 2^16 entries, 256 KiB flat, and 16 offset bits. The price is internal fragmentation and coarser protection.

The arithmetic trap is the **top level**: when the page-number bits are not a multiple of the index width, the top level is the odd one out (2 bits above).

### 4.3 Effective access time

```c
static void eat(void)
{
    double tlb = 10, mem = 100;
    printf("effective access time, TLB lookup %.0f ns, memory access %.0f ns\n", tlb, mem);
    double hits[] = { 0.80, 0.90, 0.98, 0.999 };
    for (int i = 0; i < 4; i++) {
        double h = hits[i];
        double e1 = h * (tlb + mem) + (1 - h) * (tlb + 2 * mem);             /* single-level walk: one extra access */
        double e4 = h * (tlb + mem) + (1 - h) * (tlb + 5 * mem);             /* four-level walk: four extra accesses */
        printf("  hit ratio %.3f: one-level table %6.1f ns, four-level table %6.1f ns\n", h, e1, e4);
    }
    double fault = 8e6, m = 200;
    printf("demand paging, memory %.0f ns, fault service %.0f ns (8 ms)\n", m, fault);
    double ps[] = { 0, 1e-6, 1e-5, 1e-4, 1e-3 };
    for (int i = 0; i < 5; i++) printf("  fault rate %.0e: EAT %9.1f ns (%.2fx slower)\n", ps[i], (1 - ps[i]) * m + ps[i] * fault, ((1 - ps[i]) * m + ps[i] * fault) / m);
    printf("  fault rate for at most 10%% slowdown: p < %.3e (one fault in %.0f accesses)\n", 0.1 * m / (fault - m), (fault - m) / (0.1 * m));
}
```

```text
effective access time, TLB lookup 10 ns, memory access 100 ns
  hit ratio 0.800: one-level table  130.0 ns, four-level table  190.0 ns
  hit ratio 0.900: one-level table  120.0 ns, four-level table  150.0 ns
  hit ratio 0.980: one-level table  112.0 ns, four-level table  118.0 ns
  hit ratio 0.999: one-level table  110.1 ns, four-level table  110.4 ns
```

- **Hit ratio 0.98, one level: 112 ns.** `0.98 x (10 + 100) + 0.02 x (10 + 100 + 100) = 107.8 + 4.2`. With **four levels** a miss costs `10 + 400 + 100 = 510` and the result is 118 ns.
- **Hit ratio 0.80** gives 130 ns and 190 ns: a deep page table punishes a poor TLB **much** more than a shallow one (60 ns extra against 6 ns at 0.98). This is the reason huge pages and page-walk caches matter ([OS 12](12_virtual_memory_and_tlb.md)).
- **Hit ratio 0.999** gives 110.1 and 110.4 ns: with a good TLB the page-table depth hardly matters.

### 4.4 Demand paging

```text
demand paging, memory 200 ns, fault service 8000000 ns (8 ms)
  fault rate 0e+00: EAT     200.0 ns (1.00x slower)
  fault rate 1e-06: EAT     208.0 ns (1.04x slower)
  fault rate 1e-05: EAT     280.0 ns (1.40x slower)
  fault rate 1e-04: EAT    1000.0 ns (5.00x slower)
  fault rate 1e-03: EAT    8199.8 ns (41.00x slower)
  fault rate for at most 10% slowdown: p < 2.500e-06 (one fault in 399990 accesses)
```

- A fault costing **8 ms** against a memory access of 200 ns is a ratio of 40,000. `EAT = 200 + p x (8,000,000 - 200)` approximately, so a fault rate of **1 in 10,000** (1e-4) makes memory **5 times slower** (1000 ns), and 1 in 1,000 makes it **41 times** slower.
- To keep the slowdown to 10 percent, `220 = 200 + p x 7,999,800`, so `p < 2.5 x 10^-6`: **one fault in about 400,000 accesses** (399,990 by the program's arithmetic).
- Why this matters beyond the exam: it is the quantitative reason for the working-set model and for thrashing ([OS 11](11_virtual_memory.md)). The 8 ms is a spinning disk; on an SSD `S` is 50 to 100 microseconds (from memory, typical), which changes the numbers by two orders of magnitude and not the shape of the argument.

### 4.5 A software walk of a two-level table

The classic exam question gives a tiny address format (here 4 + 4 + 8 bits) and asks for the physical address or a fault.

```c
static void walk(uint16_t va)
{
    int i1 = va >> 12, i2 = (va >> 8) & 15, off = va & 255;
    printf("  va 0x%04x: top index %2d, second index %2d, offset 0x%02x -> ", va, i1, i2, off);
    if (!(top[i1] & 1)) { printf("top entry not present: page fault (no second-level table)\n"); return; }
    int t = top[i1] >> 1;
    if (!(second[t][i2] & 1)) { printf("second entry not present: page fault\n"); return; }
    int frame = second[t][i2] >> 1;
    printf("table %d, frame 0x%02x, physical 0x%04x\n", t, frame, (frame << 8) | off);
}
```

```text
software walk of a two-level table (4 + 4 + 8 bits)
  va 0x0034: top index  0, second index  0, offset 0x34 -> table 0, frame 0x10, physical 0x1034
  va 0x0123: top index  0, second index  1, offset 0x23 -> table 0, frame 0x11, physical 0x1123
  va 0x5399: top index  5, second index  3, offset 0x99 -> table 1, frame 0x2a, physical 0x2a99
  va 0x5499: top index  5, second index  4, offset 0x99 -> second entry not present: page fault
  va 0x2000: top index  2, second index  0, offset 0x00 -> top entry not present: page fault (no second-level table)
  va 0xff77: top index 15, second index 15, offset 0x77 -> table 2, frame 0x7f, physical 0x7f77
```

How to do it by hand: split the address into top index, second index and offset (`0x5399` is `0101 0011 1001 1001`, so top 5, second 3, offset 0x99); look up the top entry (5 points to second-level table 1); look up entry 3 in that table (frame 0x2a); the physical address is the frame number followed by the offset (0x2a99). **Two distinct faults** appear: `0x5499` has a valid top entry and an absent second entry (a fault at the second level; the table exists), and `0x2000` has an absent top entry (no second-level table at all, so the kernel would have to allocate one). Naming which level faulted is half the answer.

---

## 5. Disk scheduling

### 5.1 Definitions to write first

Head movement is the number of cylinders crossed. The policies ([OS 14](14_io_systems_and_disk_scheduling.md)): **FCFS** (arrival order), **SSTF** (nearest request next), **SCAN** (the elevator: sweep in one direction to the **end of the disk**, then reverse), **C-SCAN** (sweep in one direction only, jump back to the start and sweep again), **LOOK** and **C-LOOK** (like SCAN and C-SCAN but turn around at the last **request**, not the end of the disk).

**Convention for C-SCAN and C-LOOK, stated first**: the program **counts the return jump** in the total (`199 -> 0` is 199 cylinders). Many textbooks do not count it. The answer is 382 with it and 183 for the sweep portions without it; **say which you use**.

### 5.2 The queue and the answers

The queue is `98 183 37 122 14 124 65 67`, the head is at cylinder 53, the disk has cylinders 0 to 199.

```c
static void algos(int head, int up)
{
    int s[N], ord[N + 4], k;
    memcpy(s, Q, sizeof s);
    qsort(s, N, sizeof s[0], cmp);
    printf("head at %d, moving %s, %d requests, cylinders 0 to %d\n", head, up ? "up" : "down", N, CYL - 1);
    report("FCFS", head, Q, N);
    int used[N] = { 0 }, h = head;
    for (int i = 0; i < N; i++) {
        int best = -1;
        for (int j = 0; j < N; j++) if (!used[j] && (best < 0 || abs(Q[j] - h) < abs(Q[best] - h))) best = j;
        used[best] = 1; ord[i] = Q[best]; h = Q[best];
    }
    report("SSTF", head, ord, N);
    int lo[N], nl = 0, hi[N], nh = 0;
    for (int i = 0; i < N; i++) { if (s[i] < head) lo[nl++] = s[i]; else hi[nh++] = s[i]; }
    /* SCAN: go to the end of the disk, reverse */
    k = 0;
    if (up) { for (int i = 0; i < nh; i++) ord[k++] = hi[i]; ord[k++] = CYL - 1; for (int i = nl - 1; i >= 0; i--) ord[k++] = lo[i]; }
    else { for (int i = nl - 1; i >= 0; i--) ord[k++] = lo[i]; ord[k++] = 0; for (int i = 0; i < nh; i++) ord[k++] = hi[i]; }
    report("SCAN", head, ord, k);
    /* C-SCAN: go to the end, jump to the other end, continue in the same direction */
    k = 0;
    if (up) { for (int i = 0; i < nh; i++) ord[k++] = hi[i]; ord[k++] = CYL - 1; ord[k++] = 0; for (int i = 0; i < nl; i++) ord[k++] = lo[i]; }
    else { for (int i = nl - 1; i >= 0; i--) ord[k++] = lo[i]; ord[k++] = 0; ord[k++] = CYL - 1; for (int i = nh - 1; i >= 0; i--) ord[k++] = hi[i]; }
    report("C-SCAN", head, ord, k);
    /* LOOK: reverse at the last request, not at the end of the disk */
    k = 0;
    if (up) { for (int i = 0; i < nh; i++) ord[k++] = hi[i]; for (int i = nl - 1; i >= 0; i--) ord[k++] = lo[i]; }
    else { for (int i = nl - 1; i >= 0; i--) ord[k++] = lo[i]; for (int i = 0; i < nh; i++) ord[k++] = hi[i]; }
    report("LOOK", head, ord, k);
    k = 0;
    if (up) { for (int i = 0; i < nh; i++) ord[k++] = hi[i]; for (int i = 0; i < nl; i++) ord[k++] = lo[i]; }
    else { for (int i = nl - 1; i >= 0; i--) ord[k++] = lo[i]; for (int i = nh - 1; i >= 0; i--) ord[k++] = hi[i]; }
    report("C-LOOK", head, ord, k);
}
```

```text
head at 53, moving up, 8 requests, cylinders 0 to 199
FCFS     order: 53 -> 98 -> 183 -> 37 -> 122 -> 14 -> 124 -> 65 -> 67
         total head movement 640 cylinders
SSTF     order: 53 -> 65 -> 67 -> 37 -> 14 -> 98 -> 122 -> 124 -> 183
         total head movement 236 cylinders
SCAN     order: 53 -> 65 -> 67 -> 98 -> 122 -> 124 -> 183 -> 199 -> 37 -> 14
         total head movement 331 cylinders
C-SCAN   order: 53 -> 65 -> 67 -> 98 -> 122 -> 124 -> 183 -> 199 -> 0 -> 14 -> 37
         total head movement 382 cylinders
LOOK     order: 53 -> 65 -> 67 -> 98 -> 122 -> 124 -> 183 -> 37 -> 14
         total head movement 299 cylinders
C-LOOK   order: 53 -> 65 -> 67 -> 98 -> 122 -> 124 -> 183 -> 14 -> 37
         total head movement 322 cylinders
```

How to read it:

- **FCFS 640.** The sum of absolute differences along the arrival order: 45 + 85 + 146 + 85 + 108 + 110 + 59 + 2.
- **SSTF 236.** It goes down to 37 and 14 only after 65 and 67, because 65 is nearest; SSTF is greedy, so it is **not** optimal in general (LOOK moving down beats it here with 208), and it can **starve** far requests.
- **SCAN 331.** Up to 199 (146 cylinders), then back down to 14 (185 more). The trip to 199 is the cost of "to the end of the disk"; **LOOK saves it**: 299.
- **C-SCAN 382.** 146 up to 199, 199 for the return jump, then 37 up to the last request: 146 + 199 + 37. Counting only the sweeps gives 183. **C-LOOK 322** = 130 up to 183, 169 back to 14, 23 up to 37 (jump counted).
- **Direction matters.** Moving **down** first (below), SCAN is 236 and LOOK 208, much better than moving up, because the nearer cluster of requests lies below the head.

```text
head at 53, moving down, 8 requests, cylinders 0 to 199
FCFS     order: 53 -> 98 -> 183 -> 37 -> 122 -> 14 -> 124 -> 65 -> 67
         total head movement 640 cylinders
SSTF     order: 53 -> 65 -> 67 -> 37 -> 14 -> 98 -> 122 -> 124 -> 183
         total head movement 236 cylinders
SCAN     order: 53 -> 37 -> 14 -> 0 -> 65 -> 67 -> 98 -> 122 -> 124 -> 183
         total head movement 236 cylinders
C-SCAN   order: 53 -> 37 -> 14 -> 0 -> 199 -> 183 -> 124 -> 122 -> 98 -> 67 -> 65
         total head movement 386 cylinders
LOOK     order: 53 -> 37 -> 14 -> 65 -> 67 -> 98 -> 122 -> 124 -> 183
         total head movement 208 cylinders
C-LOOK   order: 53 -> 37 -> 14 -> 183 -> 124 -> 122 -> 98 -> 67 -> 65
         total head movement 326 cylinders
```

### 5.3 Traps

1. **Whether the head goes to the end of the disk** (SCAN, C-SCAN) or stops at the last request (LOOK, C-LOOK).
2. **The initial direction** is part of the question; if it is missing, say which you assume.
3. **Whether the return jump counts** (C-SCAN, C-LOOK).
4. **Seek time is not linear in distance** in a real disk (it rises with the square root for short seeks and linearly for long ones, from memory of the usual model), and **none of this applies to an SSD**, which has no head: the scheduler's job there is fairness and batching, not seek minimisation ([OS 14](14_io_systems_and_disk_scheduling.md)).

---

## 6. File-system arithmetic

### 6.1 The classic inode

The textbook layout is 12 direct pointers, one single-indirect, one double-indirect, one triple-indirect pointer. With block size `B` and pointer size `P`, one indirect block holds `B / P` pointers, so the maximum file size is `B x (12 + n + n^2 + n^3)` with `n = B / P`.

```c
static void inode_math(long long b, long long p)
{
    long long per = b / p;
    long long d = 12 * b, s = per * b, dd = per * per * b, t = per * per * per * b;
    printf("  block %lld bytes, pointer %lld bytes: %lld pointers per block\n", b, p, per);
    printf("    12 direct %lld B, single %lld B, double %lld B, triple %lld B\n", d, s, dd, t);
    printf("    maximum file size %lld bytes = %.2f GiB (%.2f TiB)\n", d + s + dd + t, (d + s + dd + t) / 1073741824.0, (d + s + dd + t) / 1099511627776.0);
}

static void blocks_for(long long size, long long b, long long p)
{
    long long per = b / p, data = (size + b - 1) / b, meta = 0;
    if (data > 12) meta += 1;
    if (data > 12 + per) { meta += 1; long long rest = data - 12 - per; long long inner = (rest + per - 1) / per; if (inner > per) inner = per; meta += inner; }
    printf("  file of %lld bytes: %lld data blocks, %lld indirect blocks (classic layout)\n", size, data, meta);
}
```

```text
classic inode: 12 direct, one single, one double, one triple indirect pointer
  block 1024 bytes, pointer 4 bytes: 256 pointers per block
    12 direct 12288 B, single 262144 B, double 67108864 B, triple 17179869184 B
    maximum file size 17247252480 bytes = 16.06 GiB (0.02 TiB)
  block 4096 bytes, pointer 4 bytes: 1024 pointers per block
    12 direct 49152 B, single 4194304 B, double 4294967296 B, triple 4398046511104 B
    maximum file size 4402345721856 bytes = 4100.00 GiB (4.00 TiB)
  block 4096 bytes, pointer 8 bytes: 512 pointers per block
    12 direct 49152 B, single 2097152 B, double 1073741824 B, triple 549755813888 B
    maximum file size 550831702016 bytes = 513.00 GiB (0.50 TiB)
  file of 40000 bytes: 10 data blocks, 0 indirect blocks (classic layout)
  file of 1000000 bytes: 245 data blocks, 1 indirect blocks (classic layout)
  file of 100000000 bytes: 24415 data blocks, 25 indirect blocks (classic layout)
```

- **1 KiB blocks, 4-byte pointers**: n = 256, maximum 17,247,252,480 bytes = **16.06 GiB**. The triple-indirect level alone is 16 GiB.
- **4 KiB blocks, 4-byte pointers**: n = 1024, maximum 4,402,345,721,856 bytes. That is **4 TiB** from the triple level plus 4 GiB from the double level plus 4 MiB from the single level plus 48 KiB direct. The biggest term decides the answer; do the arithmetic in exact powers of two and **do not round before adding**.
- **4 KiB blocks, 8-byte pointers**: n = 512, **513 GiB**. Doubling the pointer size halves `n` and divides the triple level by 8.
- **Blocks needed**: 40,000 bytes with 4 KiB blocks is `ceil(40000 / 4096) = 10` blocks, all direct, no indirect block. 1,000,000 bytes needs 245 data blocks: 12 direct, 233 through **one** single-indirect block (so 1 indirect block). 100,000,000 bytes needs 24,415 data blocks: 12 direct, 1,024 in the single level (1 indirect), and 23,379 more through the double level, which needs 23 second-level blocks plus one top block; in total 1 + 1 + 23 = **25** indirect blocks. Count **pointer blocks as well as data blocks** when asked for space.

### 6.2 FAT

A FAT has **one entry per cluster**, so `FAT size = clusters x entry size`, where the cluster count is `disk size / cluster size`.

```c
static void fat_math(long long disk, long long cluster, int entry_bits)
{
    long long clusters = disk / cluster;
    printf("  disk %lld MiB, cluster %lld KiB, FAT%d: %lld clusters, FAT size %lld KiB\n", disk >> 20, cluster >> 10, entry_bits, clusters, clusters * entry_bits / 8 >> 10);
}
```

```text
FAT
  disk 1024 MiB, cluster 4 KiB, FAT32: 262144 clusters, FAT size 1024 KiB
  disk 1024 MiB, cluster 32 KiB, FAT32: 32768 clusters, FAT size 128 KiB
  disk 32 MiB, cluster 2 KiB, FAT16: 16384 clusters, FAT size 32 KiB
```

A 1 GiB disk with 4 KiB clusters has 262,144 clusters and, at 4 bytes per entry (FAT32), a 1 MiB table; with 32 KiB clusters the table is 128 KiB. The trade is the usual one: **bigger clusters shrink the FAT and waste more space in the last cluster of every file** (internal fragmentation), and a file's data is found by **chasing the chain** (random access walks the chain from the start unless the FAT is cached in memory).

### 6.3 What a real file system does: measured

Modern ext4 does not use block pointers by default; it uses **extents** (a start block and a length). The arithmetic above is the textbook model, and the measurement shows what actually happens to space.

```c
static long long allocated(const char *path, long long size, int sync)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    char *buf = calloc(1, 1 << 20);
    long long left = size;
    while (left > 0) { long long c = left > (1 << 20) ? (1 << 20) : left; IGN(write(fd, buf, c)); left -= c; }
    if (sync) fsync(fd);
    struct stat st;
    fstat(fd, &st);
    close(fd);
    free(buf);
    return st.st_blocks * 512;
}

static int extents(const char *path)
{
    int fd = open(path, O_RDONLY);
    struct fiemap *fm = calloc(1, sizeof *fm + 512 * sizeof(struct fiemap_extent));
    fm->fm_length = ~0ULL; fm->fm_extent_count = 512;
    int n = ioctl(fd, FS_IOC_FIEMAP, fm) < 0 ? -1 : (int)fm->fm_mapped_extents;
    close(fd);
    free(fm);
    return n;
}
```

```text
what ext4 on this machine does (the real layout uses extents, not block pointers)
  /tmp: block size 4096 bytes, 16777216 inodes in total
  write       0 bytes, fsync:        0 bytes allocated (+0 vs the size)
  write       1 bytes, fsync:     4096 bytes allocated (+4095 vs the size)
  write    4096 bytes, fsync:     4096 bytes allocated (+0 vs the size)
  write    4097 bytes, fsync:     8192 bytes allocated (+4095 vs the size)
  write  100000 bytes, fsync:   102400 bytes allocated (+2400 vs the size)
  write 1000000 bytes, fsync:  1003520 bytes allocated (+3520 vs the size)
  sparse file, ftruncate to 1 GiB: st_size 1073741824, allocated 0 bytes
  after writing one byte in the middle: allocated 4096 bytes (one 4 KiB block), extents 1
  256 MiB written sequentially: allocated 268439552 bytes, extents 5
```

- **Space is rounded up to a block**: a 1-byte file occupied 4,096 bytes; 4,097 bytes occupied 8,192; 100,000 bytes occupied 102,400; 1,000,000 bytes occupied 1,003,520. Divide by the block size (4096) and round **up**.
- **A sparse file occupies nothing**: `ftruncate` to 1 GiB gave `st_size` 1,073,741,824 and **0 bytes allocated**; writing one byte in the middle allocated one 4 KiB block in one extent. `ls -l` and `du` disagree for such files, and that is the usual question behind "why is my file 1 GiB but my disk did not fill".
- **256 MiB written sequentially** allocated 268,439,552 bytes in **5 extents**: 4,096 bytes beyond the 268,435,456 of data. That extra block is consistent with an extent-tree node, but I did not prove it. The point to take away is that a contiguous 256 MiB file needs a handful of extents where the classic layout would need roughly 65,536 data pointers and 65 indirect blocks.
- **Caveat**: `st_blocks` counts 512-byte units reserved, including metadata blocks charged to the file, and the figures depend on the file system's delayed allocation and on the `fsync` I issued before measuring.

---

## 7. Semaphore traces

### 7.1 The rules to write first

`P(s)` (wait): if the count is above zero, decrement it and continue; otherwise the caller **blocks** on the semaphore. `V(s)` (signal): if a thread is blocked on `s`, wake **one** (it completes its `P` and the count stays where it is); otherwise increment the count. The bounded buffer uses three semaphores: `mutex = 1`, `empty = N` (free slots), `full = 0` (filled slots). The **producer**: `P(empty); P(mutex); put; V(mutex); V(full)`. The **consumer**: `P(full); P(mutex); take; V(mutex); V(empty)`.

### 7.2 A correct trace

The program runs a producer and a consumer under a scheduler that lets one thread run until it blocks or finishes a burst, then switches. Here the consumer runs first with a buffer of 2 slots and 3 items.

```c
static void show(const char *who, const char *what)
{
    printf("  %-3s %-22s mutex=%d empty=%d full=%d  blocked:", who, what, sem[MUTEX], sem[EMPTY], sem[FULL]);
    int any = 0;
    for (int s = 0; s < NS; s++) for (int i = 0; i < wq_len[s]; i++) { printf(" %s(%s)", g_t[waitq[s][i]].name, SN[s]); any = 1; }
    if (!any) printf(" none");
    printf("\n");
}

static void scenario(const char *title, int good, int slots, int items, int prefer_consumer, int burst)
{
    struct th T[2];
    memset(T, 0, sizeof T);
    T[0].name = "P"; T[1].name = "C";
    build(&T[0], 0, good, items, 1);
    build(&T[1], 0, 1, items, 0);
    sem[MUTEX] = 1; sem[EMPTY] = slots; sem[FULL] = 0;
    memset(wq_len, 0, sizeof wq_len);
    g_t = T;
    printf("%s (buffer of %d slots, %d items)\n", title, slots, items);
    int cur = prefer_consumer ? 1 : 0, ran = 0, steps = 0;
    while (steps++ < 200) {
        int alive = 0;
        for (int i = 0; i < 2; i++) if (T[i].ops[T[i].pc].kind != OP_END) alive++;
        if (!alive) { printf("  both threads finished\n"); return; }
        int pick = -1;
        for (int d = 0; d < 2; d++) {
            int i = (cur + d) % 2;
            if (T[i].ops[T[i].pc].kind != OP_END && T[i].blocked_on < 0) { pick = i; break; }
        }
        if (pick < 0) { printf("  DEADLOCK: every unfinished thread is blocked\n"); return; }
        if (pick != cur) { cur = pick; ran = 0; }
        struct th *t = &T[cur];
        struct op o = t->ops[t->pc];
        if (o.kind == OP_P) {
            if (sem[o.sem] > 0) { sem[o.sem]--; t->pc++; show(t->name, o.what); }
            else { t->blocked_on = o.sem; waitq[o.sem][wq_len[o.sem]++] = cur; show(t->name, "BLOCKS"); printf("      (%s waits on %s)\n", t->name, SN[o.sem]); cur = 1 - cur; ran = 0; continue; }
        } else if (o.kind == OP_V) {
            t->pc++;
            if (wq_len[o.sem] > 0) {                           /* hand the unit straight to the first waiter */
                int w = waitq[o.sem][0];
                for (int i = 1; i < wq_len[o.sem]; i++) waitq[o.sem][i - 1] = waitq[o.sem][i];
                wq_len[o.sem]--; T[w].blocked_on = -1; T[w].pc++;
                show(t->name, o.what); printf("      (wakes %s, which completes its P(%s))\n", T[w].name, SN[o.sem]);
            } else { sem[o.sem]++; show(t->name, o.what); }
        } else { t->pc++; show(t->name, o.what); }
        if (++ran >= burst) { cur = 1 - cur; ran = 0; }
    }
}
```

```text
scenario 1: correct order, consumer runs first (buffer of 2 slots, 3 items)
  C   BLOCKS                 mutex=1 empty=2 full=0  blocked: C(full)
      (C waits on full)
  P   P(empty)               mutex=1 empty=1 full=0  blocked: C(full)
  P   P(mutex)               mutex=0 empty=1 full=0  blocked: C(full)
  P   put item               mutex=0 empty=1 full=0  blocked: C(full)
  P   V(mutex)               mutex=1 empty=1 full=0  blocked: C(full)
  P   V(full)                mutex=1 empty=1 full=0  blocked: none
      (wakes C, which completes its P(full))
  P   P(empty)               mutex=1 empty=0 full=0  blocked: none
  P   P(mutex)               mutex=0 empty=0 full=0  blocked: none
  P   put item               mutex=0 empty=0 full=0  blocked: none
  P   V(mutex)               mutex=1 empty=0 full=0  blocked: none
  P   V(full)                mutex=1 empty=0 full=1  blocked: none
  P   BLOCKS                 mutex=1 empty=0 full=1  blocked: P(empty)
      (P waits on empty)
  C   P(mutex)               mutex=0 empty=0 full=1  blocked: P(empty)
  C   take item              mutex=0 empty=0 full=1  blocked: P(empty)
  C   V(mutex)               mutex=1 empty=0 full=1  blocked: P(empty)
  C   V(empty)               mutex=1 empty=0 full=1  blocked: none
      (wakes P, which completes its P(empty))
```

Read the first rows: the consumer's `P(full)` finds `full = 0` and **blocks** (the "BLOCKS" row). The producer does `P(empty)` (2 down to 1), `P(mutex)`, puts, `V(mutex)`, and at `V(full)` finds a **blocked waiter**, so it wakes the consumer **without** raising `full` (still 0): the unit passes directly to the waiter. Later the producer fills both slots (`empty = 0`), blocks on the third `P(empty)`, and the consumer's `V(empty)` wakes it. The trace ends with both threads finished and `mutex = 1`, `empty = 2`, `full = 0`: **the counters return to their initial values**, a built-in check on any hand trace.

### 7.3 A deadlock

Swap the order in the producer (`P(mutex)` **before** `P(empty)`) and use a 1-slot buffer:

```text
scenario 2: producer takes the mutex before it waits for a free slot (buffer of 1 slots, 2 items)
  P   P(mutex)               mutex=0 empty=1 full=0  blocked: none
  P   P(empty)               mutex=0 empty=0 full=0  blocked: none
  P   put item               mutex=0 empty=0 full=0  blocked: none
  C   BLOCKS                 mutex=0 empty=0 full=0  blocked: C(full)
      (C waits on full)
  P   V(mutex)               mutex=1 empty=0 full=0  blocked: C(full)
  P   V(full)                mutex=1 empty=0 full=0  blocked: none
      (wakes C, which completes its P(full))
  P   P(mutex)               mutex=0 empty=0 full=0  blocked: none
  C   BLOCKS                 mutex=0 empty=0 full=0  blocked: C(mutex)
      (C waits on mutex)
  P   BLOCKS                 mutex=0 empty=0 full=0  blocked: C(mutex) P(empty)
      (P waits on empty)
  DEADLOCK: every unfinished thread is blocked
```

The producer holds `mutex` while it waits for a free slot, and the consumer needs `mutex` to free a slot. The sequence: the producer puts an item, releases the lock, and its `V(full)` wakes the consumer; the producer (still running its burst) loops and takes `mutex` again; the consumer, now awake, blocks on `mutex`; the producer then blocks on `empty` because the slot is still full. **Both are blocked, each holding what the other needs**: the trace ends with "every unfinished thread is blocked". This is the classic wrong-order bug, and the rule it demonstrates is **never block on a counting semaphore while holding a mutex that the other side needs to make progress** ([OS 07](07_condition_variables_and_semaphores.md), [OS 09](09_deadlocks.md)). The deadlock appears only under some schedules, which is why such bugs survive testing.

### 7.4 Other traces the interviewers use

(From memory of the standard repertoire; the tutorials above have the code.) **Readers-writers**: a reader count protected by a mutex, the first reader takes the `wrt` semaphore and the last releases it; the version shown favours readers and can starve writers. **Dining philosophers**: all five picking up the left fork first deadlocks; fixes are an ordering on forks, at most four seated at once, or an asymmetric philosopher. **Barrier** and **two-thread rendezvous**: two semaphores initialised to 0, each thread signals its own and waits for the other's; waiting before signalling deadlocks.

---

## 8. Three walkthroughs

A walkthrough question tests **ordering and naming**: which component acts, in what order, and what state changes. The method: give the chain in one breath (the skeleton), then expand the step the interviewer points at. Each walkthrough below has the skeleton, the measured evidence from this machine, and the follow-up questions that usually come next.

### 8.1 "What happens when you run `./a.out`?"

**Skeleton.** (1) The shell reads the line and parses it. (2) It calls `fork` (on Linux, `clone`); the child is a copy-on-write copy of the shell. (3) The child puts itself in a process group, sets up redirections and calls `execve("./a.out", argv, envp)`. (4) In the kernel, `execve` opens the file, checks that it is executable and that the caller may run it, reads the first bytes and picks a **binary format handler** (ELF, or `#!` script, or others). (5) For ELF the kernel **throws away the old address space**, maps the program's loadable segments, builds a **new stack** holding `argv`, `envp` and the **auxiliary vector**, and, if the file names an interpreter (`PT_INTERP`), maps the dynamic loader as well and starts there. (6) The loader maps the shared libraries, applies relocations, runs initialisers and jumps to the program's entry point (`_start`), which calls `__libc_start_main`, which calls `main`. (7) When `main` returns, `exit` runs the `atexit` handlers and flushes stdio, and the process ends with `exit_group`. (8) The parent's `wait` returns the status (a zombie until then; [OS 02](02_processes.md)).

**What `execve` keeps and drops** (from `execve(2)`, fetched): the **PID stays the same** and the process is the same process; the old program, its memory mappings and its caught-signal handlers go (caught signals revert to default; ignored ones stay ignored); **open descriptors survive** unless marked close-on-exec; the effective user ID changes for a set-user-ID program, but not under `no_new_privs`, on a `nosuid` mount or when traced (that rule is why [OS 20](20_protection_and_security.md) lists `no_new_privs`). The argument and environment strings share a limit of one quarter of the stack resource limit (with a floor of 32 pages), per the same page. A script's first line `#!` names an interpreter (255 characters since Linux 5.1; interpreters may nest to 4 levels, per the man page).

**Measured: what the process looks like at its first instruction.** The program below prints the auxiliary vector the kernel handed it, its mappings and its open descriptors.

```c
    printf("AT_PAGESZ %lu, AT_CLKTCK %lu, AT_SECURE %lu, AT_RANDOM points to 16 bytes at %p, AT_SYSINFO_EHDR (vDSO) %#lx\n",
           getauxval(AT_PAGESZ), getauxval(AT_CLKTCK), getauxval(AT_SECURE), (void *)getauxval(AT_RANDOM), getauxval(AT_SYSINFO_EHDR));
    printf("AT_ENTRY (program entry point, before main) %#lx, AT_PHDR %#lx, AT_BASE (dynamic loader base, 0 if static) %#lx\n",
           getauxval(AT_ENTRY), getauxval(AT_PHDR), getauxval(AT_BASE));
```

The program also reads `/proc/self/maps` and tries every descriptor from 0 to 9. Its output:

```text
argc 1, argv[0] ./bin/q8a_dyn, environment variables 242
AT_PAGESZ 4096, AT_CLKTCK 100, AT_SECURE 0, AT_RANDOM points to 16 bytes at 0x7ffc08efc549, AT_SYSINFO_EHDR (vDSO) 0x7f98812c9000
AT_ENTRY (program entry point, before main) 0x55e26f66b510, AT_PHDR 0x55e26f66a040, AT_BASE (dynamic loader base, 0 if static) 0x7f98812cb000
```

- **`AT_ENTRY`** is the program's entry point and lies in the program's executable mapping (the `r-xp` line below); it is **before `main`**.
- **`AT_BASE`** is where the kernel put the **dynamic loader**; it equals the start of the `ld-linux` mapping. For a **static** binary it is 0 (the static run printed `AT_BASE ... 0`): no interpreter was loaded.
- **`AT_SYSINFO_EHDR`** is the **vDSO**, a small kernel-provided shared object mapped into every process (it equals the start of the `[vdso]` line), used for calls such as `clock_gettime` that need no real system call ([OS 19](19_user_kernel_boundary.md)).
- **`AT_RANDOM`** points to 16 random bytes on the stack, the seed for the stack canary and pointer guard; **`AT_SECURE`** is 0 here because nothing made this run privileged ([OS 20](20_protection_and_security.md)); **`AT_PAGESZ`** is 4096.

```text
address-space map, one line per mapping:
  55e26f66a000-55e26f66b000         r--p q8a_dyn
  55e26f66b000-55e26f66c000         r-xp q8a_dyn
  55e26f66c000-55e26f66d000         r--p q8a_dyn
  55e26f66d000-55e26f66e000         r--p q8a_dyn
  55e26f66e000-55e26f66f000         rw-p q8a_dyn
  55e2a88dc000-55e2a88fd000         rw-p [heap]
  7f9881000000-7f9881028000         r--p libc.so.6
  7f9881028000-7f98811b1000         r-xp libc.so.6
  7f98811b1000-7f9881200000         r--p libc.so.6
  7f9881200000-7f9881204000         r--p libc.so.6
  7f9881204000-7f9881206000         rw-p libc.so.6
  7f9881206000-7f9881213000         rw-p (anonymous)
  7f98812b0000-7f98812b3000         rw-p (anonymous)
  7f98812c1000-7f98812c3000         rw-p (anonymous)
  7f98812c3000-7f98812c7000         r--p [vvar]
  7f98812c7000-7f98812c9000         r--p [vvar_vclock]
  7f98812c9000-7f98812cb000         r-xp [vdso]
  7f98812cb000-7f98812cc000         r--p ld-linux-x86-64.so.2
  7f98812cc000-7f98812f7000         r-xp ld-linux-x86-64.so.2
  7f98812f7000-7f9881301000         r--p ld-linux-x86-64.so.2
  7f9881301000-7f9881303000         r--p ld-linux-x86-64.so.2
  7f9881303000-7f9881305000         rw-p ld-linux-x86-64.so.2
  7ffc08edc000-7ffc08f05000         rw-p [stack]
  ffffffffff600000-ffffffffff601000 --xp [vsyscall]
open descriptors: 0 1 2
```

The map shows the whole result of the `execve` and the loader: five mappings for the program (read-only header, executable text, read-only data, relro, read-write data), five for libc, five for the loader, then the heap (`[heap]`), the stack, `[vvar]` and `[vdso]`, and `[vsyscall]` at the top. The **open descriptors** are 0, 1 and 2 only: whatever the shell had open was inherited, and nothing else. Addresses differ on every run because of ASLR ([OS 20](20_protection_and_security.md)); do not memorise them.

**Measured: the system calls an empty `main` makes.** The `strace` of a program whose `main` does nothing:

```text
execve("./bin/q8e_dyn", ["./bin/q8e_dyn"], 0x7ffe61c99338 /* 243 vars */) = 0
brk(NULL) = 0x55ab13bc5000
mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x7fa546def000
access("/etc/ld.so.preload", R_OK) = -1 ENOENT (No such file or directory)
openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3
fstat(3, {st_mode=S_IFREG|0644, st_size=56831, ...}) = 0
mmap(NULL, 56831, PROT_READ, MAP_PRIVATE, 3, 0) = 0x7fa546de1000
close(3) = 0
openat(AT_FDCWD, "/lib/x86_64-linux-gnu/libc.so.6", O_RDONLY|O_CLOEXEC) = 3
read(3, "\177ELF\2\1\1\3\0\0\0\0\0\0\0\0\3\0>\0\1\0\0\0\220\243\2\0\0\0\0\0"..., 832) = 832
pread64(3, "\6\0\0\0\4\0\0\0@\0\0\0\0\0\0\0@\0\0\0\0\0\0\0@\0\0\0\0\0\0\0"..., 784, 64) = 784
fstat(3, {st_mode=S_IFREG|0755, st_size=2129424, ...}) = 0
pread64(3, "\6\0\0\0\4\0\0\0@\0\0\0\0\0\0\0@\0\0\0\0\0\0\0@\0\0\0\0\0\0\0"..., 784, 64) = 784
mmap(NULL, 2174352, PROT_READ, MAP_PRIVATE|MAP_DENYWRITE, 3, 0) = 0x7fa546a00000
mmap(0x7fa546a28000, 1609728, PROT_READ|PROT_EXEC, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x28000) = 0x7fa546a28000
mmap(0x7fa546bb1000, 323584, PROT_READ, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x1b1000) = 0x7fa546bb1000
mmap(0x7fa546c00000, 24576, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x1ff000) = 0x7fa546c00000
mmap(0x7fa546c06000, 52624, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS, -1, 0) = 0x7fa546c06000
close(3) = 0
mmap(NULL, 12288, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x7fa546dde000
arch_prctl(ARCH_SET_FS, 0x7fa546dde740) = 0
set_tid_address(0x7fa546ddea10) = 3258
set_robust_list(0x7fa546ddea20, 24) = 0
rseq(0x7fa546ddf060, 0x20, 0, 0x53053053) = 0
mprotect(0x7fa546c00000, 16384, PROT_READ) = 0
mprotect(0x55ab066ba000, 4096, PROT_READ) = 0
mprotect(0x7fa546e2f000, 8192, PROT_READ) = 0
prlimit64(0, RLIMIT_STACK, NULL, {rlim_cur=8192*1024, rlim_max=RLIM64_INFINITY}) = 0
munmap(0x7fa546de1000, 56831) = 0
exit_group(0) = ?
+++ exited with 0 +++
```

The groups, in order: `execve`; `brk` and an anonymous `mmap` (the loader's early memory); `access` of `/etc/ld.so.preload` (absent); `openat` of the library cache `ld.so.cache`, `mmap` and `close`; `openat` of **libc**, `read` of its ELF header, `pread64` of its program headers, and five `mmap` calls that place its segments (text, read-only data, data and the zero-filled part); `arch_prctl(ARCH_SET_FS)` which sets up **thread-local storage**; `set_tid_address`, `set_robust_list` and `rseq` (registrations with the kernel); `mprotect` calls that make the relocated read-only data **read-only** (RELRO); `prlimit64`; `munmap` of the library cache; `exit_group`. **`main` itself made no call**, because it is empty.

```text
dyn: 30 system calls for an empty main: execve 1, openat 2, mmap 8, mprotect 3, exit_group 1
static: 15 system calls for an empty main: execve 1, openat 0, mmap 0, mprotect 1, exit_group 1
```

A dynamically linked empty program made 30 system calls and the static build made 15, mostly `mmap` and `openat` for the loader's work. That difference, and the 16 KiB dynamic binary against a roughly 800 KB static one (measured: 15,784 bytes for the empty dynamic program against about 785 to 920 KB static, depending on the program), is the honest answer to "static or dynamic?": **start-up cost and memory sharing against self-containment**. A dynamic library's read-only pages are shared between all processes that map it (they come from the same page-cache pages), and a security fix to libc applies to every program at once; a static binary starts faster, runs where the library is absent and cannot be patched without relinking.

**Follow-ups to be ready for.** *Where does the memory for the stack come from?* The kernel maps it during `execve` and the pages are faulted in on first touch. *What happens to `fork`'s copy?* It never gets used for the new program: `execve` discards it, and that is why **`vfork` and `posix_spawn` exist** (avoid even the copy-on-write set-up cost). *What is in `argv[0]`?* Whatever the caller passed, which is why `argv[0]` is not a trustworthy path. *Can `execve` fail after the old program is gone?* The permission and format checks come before the point of no return; past it, a failure kills the process instead of returning an error (from memory of the kernel's behaviour).

### 8.2 "What does `read()` do when the data is not in memory?"

**Skeleton.** (1) The program calls `read(fd, buf, n)`; libc executes the `syscall` instruction and the CPU enters the kernel ([OS 19](19_user_kernel_boundary.md)). (2) The kernel finds the **open file description** from the descriptor and calls the **VFS** `read` operation, which the file system provides (for ext4, the generic page-cache read path; from memory). (3) The kernel looks in the **page cache** for the page holding that file offset. (4) **Hit**: copy the bytes to the user buffer and return. (5) **Miss**: allocate a page, decide how much to **read ahead**, submit a **block I/O request** to the block layer, which queues and may merge it and passes it to the **device driver** (here a virtio block device). (6) The process **sleeps** (in the uninterruptible "D" state) and the CPU runs something else. (7) The device does the transfer by DMA and raises an **interrupt**; the completion handler marks the page up to date and **wakes** the sleeping process. (8) The process is scheduled again, the kernel copies the bytes to the user buffer (`copy_to_user`) and returns. ([OS 14](14_io_systems_and_disk_scheduling.md), [OS 16](16_crash_consistency_and_the_page_cache.md).)

**Measured on this machine.** The program writes a 64 MiB file, forces it to disk, then evicts it with `posix_fadvise(POSIX_FADV_DONTNEED)` (which, per the man page, frees cached pages once they are clean, hence the `fsync` first), and asks `mincore` how many pages are resident (the lowest bit of each byte, a snapshot per `mincore(2)`).

```c
static long resident(int fd)
{
    long pages = SZ / 4096, n = 0;
    void *m = mmap(NULL, SZ, PROT_READ, MAP_SHARED, fd, 0);
    unsigned char *v = malloc(pages);
    if (mincore(m, SZ, v) < 0) { perror("mincore"); n = -1; }
    else for (long i = 0; i < pages; i++) n += v[i] & 1;
    munmap(m, SZ);
    free(v);
    return n;
}

static void drop(int fd)
{
    fsync(fd);
    posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
}

static double read_at(int fd, char *buf, long off)
{
    double t0 = now();
    IGN(pread(fd, buf, 4096, off));
    return (now() - t0) * 1e6;
}
```

```text
file of 64 MiB just written: 16384 of 16384 pages resident (dirty or clean, still in the cache)
after fsync and posix_fadvise(DONTNEED): 0 pages resident
one cold 4 KiB read at 10 MiB: 211.5 us; pages resident now 1; process read_bytes grew by 4 KiB
the same read again (page cache hit): 1.69 us
the next 4 KiB page, also cold (a lone read at a random offset does not start readahead): 65.9 us
sequential 4 KiB reads from offset 0 after a drop: pages resident after 1, 2, 4, 8, 16, 32 reads: 4 4 12 12 16 48
cold random 4 KiB reads (200 of them): median 24.0 us, 90th percentile 29.0 us, max 126.3 us
warm random 4 KiB reads (200 of them): median 0.3 us, 90th percentile 0.3 us, max 2.1 us
sequential 1 MiB reads of the whole file: cold 3234 MB/s, warm 8755 MB/s
```

- **Before and after the drop**: 16,384 of 16,384 pages resident after writing (written data goes into the cache first and stays there), and **0** after `fsync` plus `DONTNEED`. So the experiment starts from a really cold file.
- **A cold read**: the first 4 KiB read took about 190 to 210 microseconds across runs (211.5 in the run shown), the **page became resident (1 page)** and the process's `read_bytes` counter rose by 4 KiB, the kernel's own evidence that the data came from storage. **The same read again took 1 to 2 microseconds** (1.69 here): a cache hit is a system call plus a copy.
- **Readahead is conditional.** The next page at a new offset took 66 microseconds, still cold: **an isolated read at a random place does not trigger readahead**. A **sequential** pattern does: after 1, 2, 4, 8, 16 and 32 reads from offset 0 the resident counts were 4, 4, 12, 12, 16 and 48 pages. The kernel reads ahead in a window that grows with a detected sequential pattern (the exact growth is the kernel's heuristic, not something to quote; `posix_fadvise` `SEQUENTIAL` doubles the window and `RANDOM` turns it off, per the man page).
- **Random cold reads of 4 KiB**: median 24 microseconds (90th percentile 29, worst 126); **warm**: median 0.3 microseconds. Cold sequential 1 MiB reads reached about 3,200 MB/s, warm about 8,750 MB/s (in other runs 2,920 to 3,234 cold and 8,317 to 8,755 warm).
- **Caveat that changes the interpretation**: the "disk" is a virtio device whose host side is itself cached, so these cold latencies are **far lower than a real SSD's read from flash** (tens to hundreds of microseconds) and orders below a spinning disk's milliseconds. The **ratio** of 100 or more between a cold and a warm read, and the **structure** (miss, readahead, wake-up), are what carry over.

**Follow-ups.** *What if the page is dirty in the cache?* A read returns the dirty data; the cache is the source of truth, and writeback happens later or on `fsync` ([OS 16](16_crash_consistency_and_the_page_cache.md)). *What does `O_DIRECT` change?* It bypasses the page cache, so every read goes to the device and you are responsible for caching and alignment. *Why is the process in D state?* It is waiting for I/O that cannot be abandoned safely, so it ignores signals until it completes (from memory; newer kernels let some waits be killable). *What is the difference with `mmap`?* A read through a mapping takes a **page fault** instead of a system call, and the fault handler consults the same page cache.

### 8.3 "What happens when you press Ctrl-C?"

**Skeleton.** (1) The keyboard produces an interrupt; the terminal driver turns the key into the byte `0x03` (the character `VINTR`). (2) The terminal's **line discipline** (in the kernel, between the driver and the process) sees that `ISIG` is on and that the byte equals `VINTR`, so it does **not** queue it for reading; it sends `SIGINT` to every process in the terminal's **foreground process group**. (3) Each such process takes the signal at its next return to user mode: default action for `SIGINT` is terminate. (4) The shell, **not** being in the foreground group while a job runs, is not signalled; its `wait` returns with "killed by signal 2" and it prints the next prompt. The echoed `^C` is the line discipline's `ECHOCTL`.

**Measured.** The program creates a pseudo-terminal, runs a mini-shell on it as a session leader, starts one **foreground** and one **background** job, then writes `0x03` to the terminal as a keyboard would.

```c
static void shell(void)
{
    struct sigaction sa = { .sa_handler = on_int };
    sigaction(SIGINT, &sa, NULL);                                  /* the shell itself catches SIGINT */
    signal(SIGTTOU, SIG_IGN);                                      /* a shell must not stop itself when it changes the foreground group */
    char b[160];
    struct termios t;
    tcgetattr(0, &t);
    snprintf(b, sizeof b, "[shell] session %d, pid %d, terminal's foreground group %d, ISIG %s, VINTR 0x%02x\n", getsid(0), getpid(), tcgetpgrp(0), (t.c_lflag & ISIG) ? "on" : "off", t.c_cc[VINTR]);
    say(b);
    pid_t bg = fork();
    if (bg == 0) { setpgid(0, 0); signal(SIGINT, SIG_DFL); for (;;) pause(); }
    setpgid(bg, bg);                                               /* background job: its own group, not foreground */
    pid_t fg = fork();
    if (fg == 0) { setpgid(0, 0); signal(SIGINT, SIG_DFL); for (;;) pause(); }
    setpgid(fg, fg);
    tcsetpgrp(0, fg);                                              /* give the terminal to the foreground job */
    usleep(100000);
    snprintf(b, sizeof b, "[shell] background job pid %d (group %d), foreground job pid %d (group %d), terminal foreground group now %d\n", bg, getpgid(bg), fg, getpgid(fg), tcgetpgrp(0));
    say(b);
    say("[shell] ready\n");
    int st;
    waitpid(fg, &st, 0);
    snprintf(b, sizeof b, "[shell] foreground job ended: %s %d (%s); shell's own handler ran %d time(s)\n", WIFSIGNALED(st) ? "killed by signal" : "exit status", WIFSIGNALED(st) ? WTERMSIG(st) : WEXITSTATUS(st), WIFSIGNALED(st) ? strsignal(WTERMSIG(st)) : "-", (int)got);
    say(b);
    tcsetpgrp(0, getpgrp());
    snprintf(b, sizeof b, "[shell] background job still alive: %s\n", kill(bg, 0) == 0 ? "yes" : "no");
    say(b);
    /* second experiment: switch the interrupt character off (ISIG off) and read the byte as data */
    tcgetattr(0, &t);
    t.c_lflag &= ~(ISIG | ICANON | ECHO);
    t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &t);
    say("[shell] ISIG off, now reading one byte\n");
    unsigned char c = 0;
    IGN(read(0, &c, 1));
    snprintf(b, sizeof b, "[shell] read byte 0x%02x as ordinary data; handler count still %d\n", c, (int)got);
    say(b);
    kill(bg, SIGKILL);
    _exit(0);
}
```

```text
[shell] session 3318, pid 3318, terminal's foreground group 3318, ISIG on, VINTR 0x03
[shell] background job pid 3319 (group 3319), foreground job pid 3320 (group 3320), terminal foreground group now 3320
[shell] ready
[keyboard] press Ctrl-C: write byte 0x03 to the terminal
^C[shell] foreground job ended: killed by signal 2 (Interrupt); shell's own handler ran 0 time(s)
[shell] background job still alive: yes
[shell] ISIG off, now reading one byte
[keyboard] press Ctrl-C again
[shell] read byte 0x03 as ordinary data; handler count still 0
```

- The **foreground job** ended "killed by signal 2 (Interrupt)", and the shell's **own SIGINT handler ran 0 times**: the shell was in a different (background) process group at that moment, because the shell gave the terminal to the job's group with `tcsetpgrp` ("terminal foreground group now 3320" is the job's group in this run).
- The **background job survived**: it was in its own group, which was not the foreground group.
- With **`ISIG` cleared** (as an editor or a password prompt does), the same byte arrived as **ordinary input data**: "read byte 0x03 as ordinary data". Ctrl-C is not magic in the keyboard; it is a **line-discipline setting** that can be turned off.
- Details visible in the code: the shell has to **ignore `SIGTTOU`** before it calls `tcsetpgrp` while it is itself in a background group, otherwise the kernel stops it (a background process that changes terminal state is sent `SIGTTOU`; from memory, and consistent with `credentials(7)`'s account of foreground and background terminal access).

**What the sources confirm and what they do not.** `termios(3)` (fetched) confirms that `ISIG` with `INTR` (Ctrl-C, 003), `QUIT` (Ctrl-\, 034) and `SUSP` (Ctrl-Z, 032) generates `SIGINT`, `SIGQUIT` and `SIGTSTP`, and the `ECHOCTL` echo. It does **not** state that the signal goes to the foreground **group**, nor document `tcsetpgrp`; those parts are from memory and are **supported by the measurement** above, not by the page.

**Follow-ups.** *Why Ctrl-C kills a pipeline entirely*: all its processes are in one group, which gets the signal. *Why a program can survive Ctrl-C*: it installed a handler or ignored `SIGINT`. *Ctrl-Z*: `SIGTSTP` stops the group and the shell regains the terminal. *Why `kill -9` cannot be caught*: `SIGKILL` and `SIGSTOP` cannot be caught, blocked or ignored. *What if there is no terminal* (a daemon): no foreground group and no keyboard signals.

---

## 9. The conceptual question bank (66 questions)

Each answer is the **two-sentence version** an interviewer wants first; the tutorial named after it holds the evidence. Answers come from the tutorials and from standard material, **from memory** unless a number is cited as measured. Practise them aloud, and expect a follow-up on any of them.

### 9.1 Processes and threads

1. **What is the difference between a process and a thread?** A process is an address space plus resources (descriptors, credentials) plus one or more threads of execution; a thread is a schedulable context (registers, stack, signal mask) inside a process. Threads of one process share memory and descriptors, so communication is cheap and a bug in one can corrupt all ([OS 03](03_threads.md)).
2. **What does `fork` copy?** It creates a child with a copy of the parent's address space (copy-on-write: pages are shared read-only until either side writes), a copy of the descriptor table pointing to the **same** open file descriptions, and the same credentials. The child gets a new PID and returns 0 from `fork` ([OS 02](02_processes.md)).
3. **Why is process creation split into `fork` and `exec`?** The gap between the two lets the child set up its descriptors, process group, signal handling and limits before the new program starts, with ordinary code and no spawn-options structure. The cost of the copy is limited by copy-on-write, and `vfork` or `posix_spawn` remove it when the child will exec at once.
4. **What are a zombie and an orphan?** A zombie has exited but has not been waited for, so its exit status and process-table entry remain; an orphan's parent has exited, and the process is re-parented (to `init` or a subreaper), which waits for it. A long-lived zombie is a parent that never calls `wait`.
5. **What does a context switch cost?** Saving and restoring registers and the kernel stack switch is small; the large costs are cache and TLB effects and, between processes, the address-space switch (partly avoided with address-space IDs). Switching between threads of one process skips the address-space change ([OS 05](05_process_scheduling.md)).
6. **User-level threads or kernel threads?** User-level threads switch fast but one blocking system call can stall the others, and they cannot use several CPUs; kernel threads are scheduled by the kernel and run in parallel. Linux uses a one-to-one model: each thread is a kernel task created with `clone` sharing the address space ([OS 03](03_threads.md)).
7. **What do threads share and what is private?** Shared: code, heap, globals, descriptors, signal dispositions. Private: registers, the stack, thread-local storage, the signal mask and the thread ID.
8. **What is PID 1's special duty?** It adopts orphans and must reap them, and the kernel treats its signals specially (it ignores signals it has no handler for). In a container whose PID 1 never calls `wait`, zombies accumulate, which is why a small init is used there.

### 9.2 Scheduling

9. **Why is SJF optimal for average waiting time, and why is it not used as is?** Running the shortest job first moves the largest total of waiting to the longest job, which minimises the sum; the measured example had SRTF at 6.50 against FCFS at 8.75. It needs the burst length in advance (only estimates from past behaviour exist) and starves long jobs.
10. **What is starvation and how does aging fix it?** A process starves when others always run ahead of it (low priority under a steady high-priority load, or long jobs under SRTF). Aging raises a waiting process's priority over time so it eventually runs.
11. **What does Linux's default scheduler do?** For normal tasks it gives each a share of CPU proportional to a weight (the nice value) and picks the task that has received the least relative to its share; recent kernels (6.6 and later) use EEVDF for the pick rule, and real-time classes (`SCHED_FIFO`, `SCHED_RR`, `SCHED_DEADLINE`) sit above it ([OS 05](05_process_scheduling.md); version detail from memory).
12. **What is priority inversion and what fixes it?** A low-priority task holds a lock that a high-priority task needs, and a medium-priority task keeps the low one from running, so the high one waits on the medium one. Priority inheritance (the holder temporarily runs at the waiter's priority) or a priority ceiling fixes it; Mars Pathfinder's 1997 resets were a well-known case (from memory).
13. **Preemptive or cooperative, and when does the kernel preempt?** Preemptive scheduling can take the CPU away (timer interrupt, a higher-priority task becoming runnable); cooperative relies on the task yielding. The kernel checks for preemption on return from an interrupt or system call and, with a preemptible kernel, at preemption points inside the kernel.
14. **How do you choose the round-robin quantum?** Too small and context-switch overhead dominates; too large and it degenerates toward FCFS and response time suffers. A common rule is that most CPU bursts should finish within one quantum, so a typical quantum is milliseconds (from memory).

### 9.3 Synchronisation

15. **Mutex, semaphore and spinlock: when each?** A mutex gives one owner at a time and blocks waiters (sleeping); a semaphore is a counter for resources or signalling and has no owner; a spinlock busy-waits and is for very short critical sections where sleeping costs more than waiting ([OS 06](06_locks.md), [OS 07](07_condition_variables_and_semaphores.md)).
16. **Why must a spinlock not be held across a sleep or taken in user space on one CPU?** A spinner on a single CPU cannot make progress until the holder runs, so it wastes its whole time slice; holding it across a sleep invites that. In the kernel, spinlocks also disable preemption while held.
17. **Why a `while` around `cond_wait`, not an `if`?** Wake-ups can be spurious, and with several waiters another thread can take the state between the signal and your wake-up. The predicate must be rechecked under the mutex ([OS 07](07_condition_variables_and_semaphores.md)).
18. **Why does a condition variable need a mutex?** The check of the predicate and the decision to sleep must be atomic with respect to the thread that changes the predicate; otherwise a signal can arrive between the check and the sleep and be lost. `cond_wait` releases the mutex and sleeps atomically.
19. **What is a lost wakeup?** A signal sent when nobody is waiting is forgotten (condition variables have no memory, semaphores do). A waiter that checks, then is signalled, then sleeps waits forever.
20. **Why do we need memory barriers?** Compilers and CPUs reorder memory accesses, and stores become visible to other CPUs in orders that differ from program order on weakly ordered machines. A barrier (or an acquire/release operation) restricts that reordering so a flag written after data is seen after the data ([OS 08](08_memory_barriers.md)).
21. **Compare-and-swap versus test-and-set, and what is ABA?** Both are atomic read-modify-write instructions; CAS can build general lock-free structures. ABA is the failure where a value changes A to B and back to A between a read and the CAS, which then succeeds although the structure changed; a version counter or hazard pointers address it.
22. **When does a readers-writer lock help?** When reads far outnumber writes and the read-side critical section is long enough to pay for the lock's own cost; with short sections the shared counter's cache-line bouncing makes it slower than a plain mutex. Writer starvation depends on the preference policy.
23. **What is a futex?** A user-space word plus a kernel wait queue: the uncontended lock and unlock are plain atomic operations in user space, and the system call happens only to sleep or wake on contention. Pthread mutexes and condition variables on Linux are built on it.

### 9.4 Deadlock

24. **State the four conditions for deadlock.** Mutual exclusion, hold and wait, no preemption, circular wait; all four must hold at once, so breaking any one prevents deadlock ([OS 09](09_deadlocks.md)).
25. **Prevention, avoidance, detection, ignorance?** Prevention breaks a condition by construction (lock ordering); avoidance grants requests only if the state stays safe (Banker's, needs advance maximums); detection finds a cycle after the fact and recovers (kill or roll back); ignoring it (the ostrich approach) is what most general-purpose systems do for ordinary resources.
26. **How does lock ordering prevent deadlock, and what is livelock?** If every thread takes locks in one global order there is no cycle. Livelock is threads that keep running but make no progress (for example both retrying and backing off in step), unlike deadlock where they are blocked.

### 9.5 Memory and virtual memory

27. **Internal versus external fragmentation?** Internal is waste inside an allocated unit (the unused tail of a page; measured: a 1-byte file used a whole 4,096-byte block); external is free space split into pieces too small to use. Paging removes external fragmentation and introduces internal ([OS 10](10_memory_management.md)).
28. **Paging versus segmentation?** Paging splits memory into fixed-size pages invisible to the program; segmentation splits it into variable-size logical pieces (code, stack) with base and limit. Modern x86-64 effectively uses paging only, with flat segments.
29. **What is in a page-table entry?** The physical frame number and flag bits: present, writable, user/supervisor, accessed, dirty, no-execute and caching attributes. The accessed and dirty bits are what page replacement and writeback use ([OS 11](11_virtual_memory.md)).
30. **What is the TLB and what happens to it on a context switch?** A small cache of recent translations; without tagging it must be flushed when the address space changes, and with address-space IDs (PCID on x86) entries from different processes coexist. A TLB miss costs a page-table walk ([OS 12](12_virtual_memory_and_tlb.md); computed in section 4: a 0.98 hit ratio gave 112 ns against 100 ns for the memory access alone).
31. **Types of page fault?** Minor (the page is in memory and only the mapping is missing, for example a first touch of zero-fill memory or a page already in the page cache), major (the page must be read from storage), and invalid (no mapping or a permission violation, which becomes `SIGSEGV`) ([OS 11](11_virtual_memory.md)).
32. **How does copy-on-write work?** Shared pages are marked read-only in both address spaces; a write faults, the kernel copies the page, maps the copy writable for the writer and resumes. After `fork` the parent and child share every page until one writes.
33. **Why does `malloc` of a large block not use memory until touched?** The allocator obtains address space (via `mmap`), but physical frames are assigned on the first touch, one fault per page (demand paging). Hence `RES` memory is lower than allocated, and a first pass over a fresh buffer is slow.
34. **What is thrashing and what is the working set?** The working set is the pages a process has used recently; when the sum over all processes exceeds memory, they fault constantly and CPU utilisation falls. The fix is to reduce the load (suspend processes) or add memory; the quantitative cause is in section 4 (computed: a fault rate of 1 in 10,000 made access 5 times slower).
35. **Why are page tables multi-level?** A flat table for a 48-bit space would be 512 GiB per process; the tree allocates only the parts that are used, and unused regions cost nothing. The price is more memory accesses per walk, which the TLB hides.
36. **`mmap` or `read` for file access?** `mmap` avoids the copy to a user buffer and lets the kernel's page cache serve the program directly, and suits random access in a large file; `read` is simpler to reason about, handles errors as return values (not `SIGBUS`) and is often as fast for sequential access ([OS 11](11_virtual_memory.md)).
37. **What is overcommit and the OOM killer?** Linux may promise more memory than exists because most allocations are never fully touched; when real memory runs out the OOM killer picks a process by a badness score and kills it. The behaviour is set by `vm.overcommit_memory`.
38. **What are huge pages good for and what do they cost?** One TLB entry covers 2 MiB instead of 4 KiB, which cuts TLB misses and walk depth for large working sets; the costs are internal fragmentation, harder allocation of contiguous memory and, for transparent huge pages, latency spikes from compaction ([OS 12](12_virtual_memory_and_tlb.md)).
39. **Where does `malloc` get memory?** From the kernel with `brk` (growing the heap) for small requests and `mmap` for large ones (the threshold is configurable, 128 KiB by default in glibc, from memory); freed memory goes back to the allocator's free lists and is returned to the kernel only in some cases.

### 9.6 File systems and I/O

40. **What does an inode contain?** Metadata (type, owner, permissions, size, timestamps, link count) and the pointers or extents to the data blocks, but **not the file name**; names live in directory entries that map name to inode number ([OS 15](15_file_systems.md)).
41. **Hard link versus symbolic link?** A hard link is another directory entry for the same inode (same file, link count rises, cannot cross file systems, not for directories); a symlink is a separate small file holding a path, which can dangle and can cross file systems.
42. **What does `open` return?** A small integer indexing the process's descriptor table, whose entry points to an **open file description** (offset, flags) that points to the inode. After `fork` or `dup` descriptors share the same description and hence the same offset ([OS 15](15_file_systems.md)).
43. **What does `write` guarantee and what does `fsync` add?** `write` copies data into the page cache and returns; it is not durable. `fsync` forces the file's data and the metadata needed to find it to stable storage before returning ([OS 16](16_crash_consistency_and_the_page_cache.md)).
44. **What does a journal protect?** It makes **metadata** updates atomic across a crash (replay or discard the log), so the file system structure stays consistent; data journaling also protects data, at a cost, and the default ordered mode orders data before the metadata that references it (from memory for ext4's modes).
45. **When would you use `O_DIRECT`?** When the application has its own cache (a database) and wants to avoid double caching and the page cache's eviction behaviour; it needs aligned buffers and sizes and gives up readahead and write-back batching.
46. **Polling, interrupts and DMA?** Polling spins on a device status (low latency, wastes CPU); interrupts notify on completion (efficient for slow devices, costly under high rates); DMA lets the device move data to memory without the CPU copying each word. High-rate devices combine them (interrupt coalescing, NAPI-style polling) ([OS 14](14_io_systems_and_disk_scheduling.md)).
47. **Why does disk scheduling matter less on SSDs?** There is no head to move, so seek-minimising orders are pointless; the I/O scheduler is mainly about fairness, merging and deadlines, and many systems use a minimal one (`none` or `mq-deadline`).
48. **What is readahead?** The kernel reads pages ahead of a detected sequential pattern so later reads hit the cache (measured: a lone random read did not trigger it, a sequential run did).
49. **What is the VFS?** The kernel's common file-system interface (inode, dentry, file and superblock objects with operation tables) that lets `read` and `open` work the same on ext4, XFS, NFS or procfs ([OS 15](15_file_systems.md)).

### 9.7 IPC

50. **Pipe, socket, shared memory: which is fastest and why?** Shared memory, because after setting it up the data is read and written without system calls or copies; pipes and sockets copy through the kernel on each transfer. Its price is that you must synchronise yourself ([OS 17](17_ipc_fundamentals.md), [OS 18](18_ipc_mechanisms.md)).
51. **Why does shared memory need synchronisation and what do you use?** Two processes updating the same location race, and visibility is not ordered without barriers. Use process-shared mutexes or semaphores, futexes or atomics with the right memory order.
52. **What can a signal handler safely do?** Only call async-signal-safe functions (a short list: `write`, `_exit` and so on, not `printf` or `malloc`), or set a `volatile sig_atomic_t` flag; the handler can interrupt the program anywhere, including inside `malloc`.
53. **How does a pipe behave when full or closed?** A writer blocks when the buffer is full; a reader sees end-of-file only when **every** write end is closed; writing to a pipe with no reader raises `SIGPIPE` or fails with `EPIPE`. A forgotten open write end is the usual reason a reader hangs.
54. **What do Unix domain sockets offer that pipes do not?** Bidirectional communication, message boundaries (datagram type), passing **file descriptors** between processes and verifying the peer's credentials ([OS 18](18_ipc_mechanisms.md)).

### 9.8 Protection and the kernel boundary

55. **What happens on a system call?** The `syscall` instruction switches the CPU to kernel mode at an entry point set by the kernel, the kernel saves user state, switches to a kernel stack, dispatches on the system-call number, validates arguments and returns with `sysret`/`iret` ([OS 19](19_user_kernel_boundary.md)).
56. **Why is a system call more expensive than a function call?** It crosses a privilege boundary: the mode switch, saving state, argument checking and, on affected processors, mitigations for speculative-execution attacks. A seccomp filter adds a little more (measured in [OS 20](20_protection_and_security.md): about 10 nanoseconds for a number-only filter).
57. **What is the vDSO?** A small kernel-supplied shared object mapped into every process (the `[vdso]` line above) that implements calls like `clock_gettime` in user space reading data the kernel updates, avoiding a real system call.
58. **Interrupt, exception and trap?** An interrupt is asynchronous from a device or timer; an exception is synchronous from the current instruction (a page fault, a divide error); a trap is an intentional exception such as a system call or breakpoint.
59. **What is a set-user-ID program and what is the risk?** It runs with the owner's identity (often root) whatever the caller, so any flaw in it (path, environment, buffer) becomes privilege escalation; it should drop privilege as early as possible and `no_new_privs`, `nosuid` mounts and capabilities narrow the exposure ([OS 20](20_protection_and_security.md)).
60. **What do ASLR, NX and stack canaries each stop?** ASLR randomises addresses so an attacker cannot rely on fixed locations (measured: 28 bits for mmap and 22 for the stack on this kernel); NX stops execution of data pages (an injected-code attempt hit `SEGV_ACCERR`); a canary detects a linear stack overwrite before the function returns. None stops an attack that only changes data, and return-oriented attacks reuse existing code to get round NX, which is why further defences exist ([OS 20](20_protection_and_security.md)).
61. **What is a Linux capability?** A privilege split out of "root" (such as binding low ports or setting the clock) that can be granted alone; a process can hold only the capabilities it needs. Root without a capability cannot do that operation (measured: this sandbox's root lacked `CAP_SYS_RESOURCE`).
62. **What does seccomp do?** It lets a process restrict the system calls it (and its descendants) may make by installing a filter, shrinking the kernel attack surface; the filter is installed after `no_new_privs` (without it the install failed with `EACCES` in the measurement) ([OS 20](20_protection_and_security.md)).
63. **What are namespaces and cgroups, and how do containers use them?** Namespaces give a process its own view of PIDs, mounts, network, users and more; cgroups limit and account resources (CPU, memory, I/O). A container is ordinary processes with those restrictions, not a lightweight VM, so it shares the host kernel ([OS 20](20_protection_and_security.md)).
64. **Which operations are privileged?** Those that touch hardware or global state: I/O port access, page-table and control-register changes, interrupt control, halting the CPU. User code attempting them takes an exception, so the kernel stays in control ([OS 01](01_os_structure_and_system_calls.md)).
65. **Why must the kernel use `copy_from_user`?** A user pointer may be invalid, unmapped or point at kernel memory, and the user can change the memory concurrently; the helpers check the range and handle faults safely, and checking once then reading again is a time-of-check to time-of-use hole ([OS 19](19_user_kernel_boundary.md)).
66. **Monolithic kernel versus microkernel?** A monolithic kernel (Linux) runs file systems, drivers and networking in kernel mode for speed, at the price of a larger trusted base; a microkernel keeps only scheduling, memory and IPC in the kernel and runs services as user processes for isolation, at the price of more message passing ([OS 01](01_os_structure_and_system_calls.md)).

---

## 10. What it costs: the numbers worth remembering

Computed values are exact answers to the instances above; measured values come from this machine and carry the 10 to 30 percent run-to-run spread stated at the top.

| Item | Value | Kind |
| --- | --- | --- |
| Average waiting, FCFS / SJF / SRTF / RR(3) / priority | 8.75 / 7.75 / 6.50 / 13.50 / 7.75 | computed |
| Banker's sample: safe sequence | P1, P3, P0, P2, P4 | computed |
| Faults, 20-page string, 3 frames: FIFO / LRU / OPT / clock | 15 / 12 / 9 / 14 | computed |
| Belady string, FIFO with 3 then 4 frames | 9 then 10 faults | computed |
| Flat page table, 32-bit, 4 KiB pages, 4-byte entries | 4 MiB (two levels of 10 bits) | computed |
| Levels for 48-bit and 57-bit addresses, 8-byte entries | 4 and 5 | computed |
| EAT, TLB 10 ns, memory 100 ns, hit 0.98, one level / four levels | 112 / 118 ns | computed |
| Fault rate allowing a 10 percent slowdown at 8 ms per fault | below 2.5e-06 (1 in 399,990) | computed |
| Disk, classic queue, head 53 up: FCFS / SSTF / SCAN / C-SCAN / LOOK / C-LOOK | 640 / 236 / 331 / 382 / 299 / 322 | computed |
| Classic inode maximum, 4 KiB blocks, 4-byte pointers | 4,402,345,721,856 bytes | computed |
| ext4 allocation for a 1-byte file; a 1 GiB sparse file | 4,096 bytes; 0 bytes | measured |
| System calls of an empty `main`, dynamic / static | 30 / 15 | measured |
| Cold 4 KiB read, then the repeat | about 190 to 210 microseconds, then 0.6 to 1.7 | measured |
| Cold random 4 KiB read median, warm median | 24 to 26, 0.3 microseconds | measured |

---

## 11. Common mistakes

1. **Starting to compute before writing the formula and the convention**, then being unable to say why the answer differs from the key.
2. **Waiting time computed as completion minus arrival** (that is turnaround); subtract the burst.
3. **Round robin with a wrong tie rule** for a process that arrives exactly when a quantum expires; state your rule.
4. **Counting preemption in SRTF for equal remaining times**; only a strictly shorter remainder preempts.
5. **Banker's: forgetting that `need = max - allocation`**, or taking `need` from the wrong row.
6. **Banker's: calling an unsafe state a deadlock.** Unsafe means a deadlock is possible, not that one exists.
7. **LRU victim chosen by frame position** instead of by last use; and **OPT with ties** treated as an error.
8. **Assuming more frames always means fewer faults**; Belady's anomaly says that is false for FIFO.
9. **Page-table size computed from the wrong entry count**: use the number of pages (address bits minus offset bits), not the number of bytes.
10. **EAT without the TLB lookup on a miss, or without the final data access.** A miss costs the lookup, the walk and the data access.
11. **Mixing nanoseconds and milliseconds in the demand-paging formula.** 8 ms is 8,000,000 ns.
12. **SCAN or C-SCAN totals that ignore whether the head goes to the end of the disk, and whether the return jump counts.** State both.
13. **Inode size arithmetic rounded too early**; keep exact powers of two and add last. And forgetting the pointer blocks when counting space.
14. **Semaphore traces that raise the count on a `V` that woke a waiter.** A `V` with a waiter wakes it and leaves the count alone.
15. **Walkthroughs that skip a layer** (no page cache, no sleep, no interrupt) or confuse the **shell's** signal handling with the **foreground group's**.
16. **Quoting measured numbers as universal.** A latency from a VM is a shape, not a constant.

---

## 12. Questions out loud

Say each answer aloud in under a minute, with one number or one named mechanism in it.

1. Run the safety test on a 5-process Banker's state and explain what the result does and does not mean.
2. Why is SRTF optimal for average waiting, and what two problems does it have?
3. Show Belady's anomaly and say why LRU cannot have it.
4. Derive the levels of a 48-bit page table from page size and entry size.
5. Compute an effective access time with a TLB and say which part dominates at a poor hit ratio.
6. What fault rate can a system tolerate if a fault costs 8 ms and memory 200 ns?
7. Explain the difference between SCAN, C-SCAN, LOOK and C-LOOK, and what you assume about the return jump.
8. How large can a file be with 12 direct pointers and three levels of indirection? Which term dominates?
9. Why does ext4 report 0 allocated bytes for a 1 GiB sparse file?
10. Trace a bounded buffer to a deadlock with the producer's two waits in the wrong order.
11. Walk through `./a.out` from the shell prompt to `main`, naming the kernel's work and the loader's work.
12. What happens in a `read()` that misses the page cache, and why is a lone random read slow while a sequential read is fast?
13. What does Ctrl-C do, and why does the shell survive it?
14. Pick any question from section 9 and give the two-sentence answer plus one follow-up the interviewer might ask.

---

## Problems

**Where to solve them.** On paper with a timer, using the targets in section 0; then check against your own small program in any language (each is a few dozen lines, and writing one is the best test of whether you know the rule). For more practice instances with a simulator that checks you, the public OSTEP homework repository has scheduler, paging, disk and file-system simulators (the page and its scheduler, paging, disk and file-system folders were confirmed by fetching; I did not run them).

**How to approach them.** Follow the method in section 0: formula and convention first, a visible working, a sanity bound, then the answer with units. For Problem E, treat the two parts as independent.

### Problem A (E, entry level): scheduling

Processes (arrival, burst): P1 (0, 5), P2 (1, 3), P3 (2, 1), P4 (3, 2). Compute the average waiting time under FCFS and under SRTF, and draw the SRTF Gantt chart (one character per tick).

### Problem B (E, entry level): page replacement

Reference string `1 2 3 2 4 1 5 2 1 2 3 4` with 3 frames. Count the faults under FIFO, LRU and OPT, and give the minimum possible number of faults for any policy.

### Problem C (M, medium): translation and EAT

A machine has a 40-bit virtual address, 8 KiB pages and 8-byte page-table entries. (a) How many bits are the offset and the page number? (b) What is the size of a flat page table? (c) If each table occupies one page, how many levels are needed and how many index bits does each have? (d) With a TLB lookup of 20 ns, a memory access of 80 ns, a hit ratio of 0.95 and the multi-level table from (c), what is the effective access time?

### Problem D (M, medium): Banker's algorithm

Total resources (6, 4, 5). Four processes with allocation and maximum:

```text
      allocation   maximum
P0    (1,0,1)      (3,2,2)
P1    (2,1,0)      (4,2,1)
P2    (1,1,1)      (2,2,2)
P3    (0,1,2)      (2,3,3)
```

(a) Compute `need` and `available`. (b) Is the state safe? Give a safe sequence. (c) P3 requests (1,1,0): can it be granted?

### Problem E (H, hard): file system and disk

(a) A file system has 2 KiB blocks, 4-byte block pointers and inodes with 10 direct pointers, one single-indirect and one double-indirect pointer. What is the maximum file size, and how many blocks in total (data plus pointer blocks) does a 5 MiB file need? (b) A disk has cylinders 0 to 199; the head is at 50 moving down; the queue is `82 170 43 140 24 16 190`. Give the total head movement under FCFS, SSTF, SCAN (to the end of the disk) and LOOK.

---

## Answers

**A.** FCFS: waiting times 0, 4, 6, 6 for an average of **4.00**. SRTF: the Gantt chart is `12322441111` (P1 runs one tick; P2 preempts at 1; P3 preempts at 2 and finishes at 3; P2 finishes at 5; P4 runs 5 to 7; P1 finishes at 11). Waiting times are 6, 1, 0 and 2, an average of **2.25**. (Computed by a short script and checked by hand.)

**B.** FIFO **9** faults, LRU **9**, OPT **7**. The string touches 5 distinct pages (1, 2, 3, 4, 5), so **5** is the minimum for any policy. OPT's 7 is at most the others', as it must be.

**C.** (a) Offset 13 bits (8 KiB); page number 40 - 13 = **27** bits. (b) 2^27 entries x 8 bytes = **1 GiB**. (c) A table page holds 8192 / 8 = 1024 entries, so 10 index bits per level; 27 / 10 needs **3 levels** (10 + 10 + 7 bits). (d) A hit costs 20 + 80 = 100 ns; a miss costs 20 + 3 x 80 + 80 = 340 ns; `EAT = 0.95 x 100 + 0.05 x 340 =` **112 ns**.

**D.** (a) Allocations total (4,3,4), so `available = (6,4,5) - (4,3,4) =` **(2,1,1)**. Needs: P0 (2,2,1), P1 (2,1,1), P2 (1,1,1), P3 (2,2,1). (b) **Safe**: start with work (2,1,1); P1's need (2,1,1) fits, work becomes (4,2,1); P0 (2,2,1) fits, (5,2,2); P2 (1,1,1) fits, (6,3,3); P3 (2,2,1) fits, (6,4,5), the total. The sequence **P1, P0, P2, P3** works (others may too). (c) P3's request (1,1,0) is within its need and within the available, but granting it leaves available (1,0,1) and no process's need fits: **unsafe, so it must wait**. (Confirmed by a script.)

**E.** (a) Pointers per block 2048 / 4 = 512. Maximum size = 2048 x (10 + 512 + 512^2) = **537,939,968 bytes**. A 5 MiB file is 2,560 data blocks: 10 direct, 512 through the single-indirect block (1 pointer block), and the remaining 2,038 through the double-indirect block, which needs 4 second-level blocks (2,038 / 512 rounds up to 4) plus the top block: 5. Pointer blocks 1 + 5 = 6, so **2,566 blocks** in total. (b) FCFS **642** (in queue order: 50 to 82 is 32, to 170 is 88, to 43 is 127, to 140 is 97, to 24 is 116, to 16 is 8, to 190 is 174; the sum is 642). SSTF **208** (order 43, 24, 16, 82, 140, 170, 190). SCAN **240** (50 down to 0 is 50, then up to 190 is 190). LOOK **208** (50 down to 16 is 34, then up to 190 is 174). SSTF and LOOK tie here by coincidence of the data. (Computed by a script.)

---

## Cheat sheet

- **Write first**: formula, units, convention; **last**: sanity bound.
- **Scheduling**: turnaround = finish - arrival; waiting = turnaround - burst; response = first run - arrival. SRTF minimises average waiting; round robin gives the best response and the worst waiting; state the tie rule.
- **Banker's**: `need = max - alloc`; `available = total - sum(alloc)`; repeat: pick a process with `need <= work`, add its allocation. Unsafe is not deadlocked. Grant a request only if it is within need, within available **and** the pretended state is safe.
- **Paging**: cold faults = distinct pages; OPT <= LRU <= (usually) FIFO; stack algorithms (LRU, OPT) have no Belady anomaly; FIFO does.
- **Translation**: pages = 2^(address bits - offset bits); flat table = pages x entry size; index bits per level = log2(page / entry); EAT = h(t + m) + (1 - h)(t + L m + m); demand paging EAT = (1 - p) m + p S.
- **Disk**: SCAN goes to the end, LOOK to the last request; C-variants jump back and sweep one way; state whether the jump counts and the initial direction.
- **Inode**: n = block / pointer; max = block x (direct + n + n^2 + n^3); count pointer blocks; FAT size = clusters x entry size.
- **Semaphore**: `V` with a waiter wakes it and leaves the count; never wait on a counting semaphore while holding the lock the other side needs.
- **Walkthroughs**: `./a.out` is fork, exec (kernel maps segments, stack, auxv, loader), loader maps libraries, `_start`, `main`, `exit_group`, `wait`; a cold `read` is syscall, VFS, page-cache miss, readahead, block layer, driver, sleep, interrupt, wake, copy; Ctrl-C is a byte turned into `SIGINT` for the foreground **group** by the line discipline.
- **Order of confidence** for any claim: measured or computed here, fetched from a manual page, from memory. Say which.

---

## Sources

Fetched in this session (what each confirmed):

- [execve(2)](https://man7.org/linux/man-pages/man2/execve.2.html): the program is replaced and the PID kept, descriptors survive unless close-on-exec, caught signals reset, set-user-ID ignored under `no_new_privs`, `nosuid` or tracing, the argument size limit, `#!` interpreter lines and `PT_INTERP`. It does not discuss the auxiliary vector.
- [getauxval(3)](https://man7.org/linux/man-pages/man3/getauxval.3.html): the auxiliary-vector entries (`AT_PAGESZ`, `AT_CLKTCK`, `AT_SECURE`, `AT_RANDOM`, `AT_SYSINFO_EHDR`, `AT_ENTRY`, `AT_PHDR`, `AT_BASE`).
- [posix_fadvise(2)](https://man7.org/linux/man-pages/man2/posix_fadvise.2.html): `DONTNEED` frees cached pages (dirty pages need `fsync` first), `SEQUENTIAL` doubles readahead, `RANDOM` disables it, `WILLNEED` starts a non-blocking read.
- [mincore(2)](https://man7.org/linux/man-pages/man2/mincore.2.html): the lowest bit of each result byte says whether the page is resident, as a snapshot.
- [termios(3)](https://man7.org/linux/man-pages/man3/termios.3.html): `ISIG` with `INTR`, `QUIT` and `SUSP` generating `SIGINT`, `SIGQUIT` and `SIGTSTP`; `ECHOCTL`. It does not state foreground-group delivery and does not document `tcsetpgrp`.
- [credentials(7)](https://man7.org/linux/man-pages/man7/credentials.7.html): sessions, process groups and foreground-job terminal access.

From memory, not fetched in this session: the textbook definitions and conventions of sections 1 to 7, the standard instances (the 20-page string, the Banker's example, the disk queue, Belady's string), the conceptual answers of section 9 beyond those cited to tutorials.

Links whose pages were fetched only to confirm they exist: OSTEP, the Arpaci-Dusseau [operating-systems textbook](https://pages.cs.wisc.edu/~remzi/OSTEP/), and its [homework simulators](https://github.com/remzi-arpacidusseau/ostep-homework); their contents were not studied. Measured values are from this session's programs on one Firecracker VM and are not benchmarks of any other machine.
