# CO 18: Multicore and Multithreading

*Part of the [Computer Organization plan](00_plan.md) · Core tutorial · Time: about 4 hours (2 reading, 2 problems) · You need: [tutorial 11](11_superscalar_and_out_of_order_execution.md) (issue slots, instruction-level parallelism) and [tutorial 16](16_dram_memory_controllers_and_numa.md) (bandwidth, Little's law, NUMA)*

By the end of this tutorial you can:

- explain with the power equation why processors went from one fast core to many slower ones, and compute the power and ideal speedup of a multicore design;
- classify machines with Flynn's taxonomy (SISD, SIMD, MISD, MIMD) and say where multicore CPUs, vector units and GPUs fit;
- explain coarse-grained, fine-grained and simultaneous multithreading (SMT), what a hardware thread shares with its sibling, and when SMT helps, does nothing or hurts;
- compare shared-memory (SMP, NUMA) and distributed-memory machines, and say what the uncore, a chiplet design and a heterogeneous (big and little) design change for software;
- name the three parallel programming models (shared address space, message passing, data-parallel) and split work between threads statically, dynamically or by stealing;
- apply Amdahl's law, Gustafson's law and the Karp-Flatt metric, and list what else limits scaling: synchronisation, memory bandwidth, frequency, imbalance, oversubscription;
- read the topology of a machine (`lscpu`, `lstopo`, `sysctl`) and size a thread pool.

Tutorials 9 to 16 followed one stream of instructions through one core and its memory. A modern chip has 4 to 128 cores, and each core may run two threads. The hardware gives you the cores; **the program has to supply the parallelism**, and most of the work in this tutorial is arithmetic about how much of it you can use. Tutorial 19 (coherence) and tutorial 20 (memory consistency) then explain how the cores agree about shared data.

**Where the numbers come from.** The power, Flynn, SMT, Amdahl and scheduling arithmetic is exact and was checked with a script. Timings marked *measured* were taken on the cloud virtual machine of tutorials 14 to 17: an Intel Xeon under KVM with **two** virtual CPUs. Two CPUs can show 1 thread against 2 threads, Amdahl's law, load balancing, thread start-up cost and a contended counter, but they cannot show a curve out to 16 or 64 cores or hit a memory-bandwidth wall; for those I use the arithmetic of tutorial 16 and say so. Cloud numbers drift between runs, so measured figures are ranges over three runs.

> **The one-sentence model.** Power grows with the cube of frequency, so the hardware spends its transistors on many cores (and on hardware threads that fill a core's idle slots); the speedup you get is then set by the part of the program that cannot run in parallel, by the shared resources the threads fight over (memory bandwidth, locks, cache lines), and by how evenly you divide the work.

---

## 1. Why many cores: the power wall

### 1.1 The power equation

A chip's **dynamic power** is the power spent switching transistors:

```text
P_dynamic = α × C × V² × f
   α = activity (fraction of gates that switch each cycle)     C = switched capacitance
   V = supply voltage                                           f = clock frequency
```

On top of it a chip burns **static (leakage) power** just by being powered, whatever it does. All of the power turns into heat, and a package and a cooler can remove only so many watts per square centimetre.

A faster clock needs a higher voltage (the transistors must charge faster), so over the normal operating range *V* rises about in proportion to *f*, and

```text
P ∝ V² × f ∝ f³        (the cube law, valid over the voltage range where V can follow f)
```

**Worked example.** Take one core at frequency 1.0 as the baseline (performance 1, power 1) and assume a program that parallelises perfectly. Power is *N* × *f*³, ideal performance is *N* × *f*:

| Design | Per-core frequency | Ideal performance | Power | Performance per watt |
|---|---|---|---|---|
| 1 core | 1.0 | 1.00 | 1.00 | 1.00 |
| 1 core, clock raised | 1.3 | 1.30 | 2.20 | 0.59 |
| 1 core, clock raised more | 1.6 | 1.60 | 4.10 | 0.39 |
| 2 cores | 0.8 | 1.60 | 1.02 | 1.56 |
| 4 cores | 0.6 | 2.40 | 0.86 | 2.78 |
| 8 cores | 0.4 | 3.20 | 0.51 | 6.25 |

Reading the table: 1.6 times the performance from the clock alone costs 4.1 times the power; from two slower cores it costs about the same power as the single core. Four cores at 0.6 of the frequency beat the original by 2.4 times for *less* power. The price is the word "ideal": the extra cores help only if the software can use them (sections 5 and 6).

### 1.2 Why the cube law stops, and what that means

- **Voltage has a floor.** Below a minimum voltage transistors do not switch reliably. Below that point lowering *f* saves power only linearly, and leakage does not fall at all.
- **Dennard scaling ended.** For a long time each smaller transistor generation let designers cut *V* and *C* so that power per square millimetre stayed constant while *f* rose. When *V* could no longer be cut (leakage), smaller transistors kept their power density and a chip became **power-limited rather than transistor-limited**: you can build more transistors than you can afford to switch at full speed together. The part that must stay idle at any moment is called **dark silicon**.
- **One bigger core runs out of work to do.** Tutorials 11 and 12 showed that a wider, deeper out-of-order core finds less and less extra instruction-level parallelism for each extra transistor, and the L3 and DRAM latencies of tutorials 13 to 16 do not shrink with the clock.

So the budget goes elsewhere: more cores at moderate frequency, bigger caches, specialised engines (tutorials 21 to 24), and **hardware threads** that keep a core busy (section 3). The consequence for programmers is the whole subject of this tutorial: performance that used to arrive with the next processor now has to be earned by parallel software.

### 1.3 Frequency depends on how many cores are busy

The power and heat budget is shared by the whole chip, so the highest sustained clock falls as more cores are active. A part may run one core at its top boost clock and all cores at a lower one. That bends every scaling curve: if the all-core clock is 0.8 of the single-core clock, 16 cores can give at most 16 × 0.8 = 12.8 times the single-core speed even for perfectly parallel code. Always compare a scaling result with the frequency each run actually had (`perf stat` reports cycles, and cycles per second is the frequency).

---

## 2. Flynn's taxonomy

Flynn classified machines by the number of **instruction streams** and **data streams** they handle at once:

| Class | Meaning | Examples |
|---|---|---|
| **SISD** | one instruction stream, one data stream | a scalar core running one thread. An out-of-order superscalar core is still SISD as far as the programmer can tell: it finds parallelism internally but presents one sequential instruction stream |
| **SIMD** | one instruction applied to many data items | vector instructions (SSE, AVX, NEON, SVE: [tutorial 21](21_simd_and_vector_processing.md)); a GPU warp executes one instruction for 32 threads at a time (NVIDIA calls this SIMT, [tutorial 22](22_gpu_execution_model.md)) |
| **MISD** | several instruction streams on one data stream | rare. Sometimes cited: redundant processors running in lockstep that vote on one input (safety-critical systems), and systolic pipelines (arguable) |
| **MIMD** | independent instruction streams on independent data | multicore CPUs, multi-socket servers, clusters of machines |

In practice MIMD machines are programmed **SPMD** (single program, multiple data): every thread or process runs the same program and takes a different part of the data (OpenMP loops, MPI ranks, GPU kernels). A real server mixes the classes: MIMD across its cores, SIMD inside each core's vector unit, and a SISD-looking instruction stream per thread. This tutorial is about MIMD; [tutorial 21](21_simd_and_vector_processing.md) is SIMD and tutorials [22](22_gpu_execution_model.md) and [23](23_gpu_memory_tensor_cores_and_scaling.md) are SIMT.

**Worked classification.** An 8-core laptop running a compiler: MIMD. The same laptop running `memchr` with AVX2 on one thread: SIMD inside a SISD thread. A GPU doing a matrix multiply: SIMT, i.e. SIMD groups scheduled MIMD-style across many streaming multiprocessors. A cluster of 100 servers each running its own request handler: MIMD with distributed memory.

---

## 3. Hardware threads: multithreading inside a core

### 3.1 The problem

A core has several issue slots per cycle (4 to 8 on a modern core), but a single thread rarely fills them. It waits for loads (a DRAM miss is about 300 cycles, tutorial 16), for mispredicted branches to recover and for long dependence chains (tutorial 11), and even on a good day it fills a fraction of its slots. Out-of-order execution hides some of that by looking ahead within one thread. **Multithreading** adds a second source of independent work: keep the state of several threads in the core and issue from whichever has something ready. Instructions from different threads have no dependences on each other, so there is nothing to check.

### 3.2 Three styles

| Style | Which thread issues in a cycle | What it hides | Where used |
|---|---|---|---|
| **Coarse-grained** | one thread at a time; switch only on a long stall such as a cache miss | long stalls (DRAM) | simple designs; a thread switch costs a few cycles of pipeline refill |
| **Fine-grained** | a different ready thread every cycle (round robin, skipping stalled ones) | all stalls, even short ones, without bypassing or branch prediction | GPUs (a scheduler picks a ready warp every cycle, [tutorial 22](22_gpu_execution_model.md)), some DSPs; poor single-thread speed |
| **Simultaneous (SMT)** | several threads in the *same* cycle: the issue slots are filled from more than one thread | stalls *and* the empty slots inside a cycle | out-of-order CPUs: Intel Hyper-Threading and AMD SMT (2 threads per core), IBM POWER (4 or 8); most Arm cores and Apple's cores have none |

Here is the difference in issue slots, with toy numbers: a 4-wide core, 6 cycles; `A` and `B` are instructions of threads A and B, `.` is an empty slot. Thread A has little parallelism and a cache miss that stalls it in cycles 3 and 4.

```text
 A alone (8 of 24 slots used, 33%)         A and B with SMT (21 of 24 slots used, 88%)
 cycle 1:  A A . .                         cycle 1:  A A B B
 cycle 2:  A . . .                         cycle 2:  A B B B
 cycle 3:  . . . .   <- A waits for DRAM   cycle 3:  B B . .
 cycle 4:  . . . .                         cycle 4:  B B B .
 cycle 5:  A A A .                         cycle 5:  A A A B
 cycle 6:  A A . .                         cycle 6:  A A B B
```

B fills the slots that A leaves empty: both the stall (cycles 3 and 4) and the unused slots inside busy cycles. A coarse-grained core would use cycles 3 and 4 for B but still waste the empty slots in cycles 1, 2, 5 and 6. (Toy numbers: B is shown as having plenty of work, and A is shown unslowed; on a real core B takes some of A's queue entries and ports, so each thread runs slower than it would alone.)

### 3.3 What a hardware thread shares

With SMT the operating system sees each hardware thread as a **logical CPU**; `lscpu` shows "Thread(s) per core: 2". The core duplicates only the small per-thread state and shares the rest:

| Duplicated per thread (cheap: a few percent of core area) | Shared, partitioned or competed for |
|---|---|
| program counter, architectural registers and rename map, return-address stack, interrupt state | execution ports and ALUs, the L1 and L2 caches, TLBs (entries tagged by thread), branch-predictor tables, reorder buffer, load and store queues, memory bandwidth |

Everything in the right-hand column is a resource the two threads fight over, which is why the gain is bounded and why a bad neighbour can hurt.

### 3.4 How much does SMT help?

Measure it as the **weighted speedup**: the sum over the threads of (IPC with SMT) ÷ (IPC when alone). Running two threads one after the other (or time-sliced) gives 1.0.

| Workload pair | Alone | Together | Weighted speedup |
|---|---|---|---|
| two pointer-chasing threads (latency-bound, IPC 0.25 alone) | 0.25 each | 0.24 each | 0.96 + 0.96 = **1.92** |
| a typical mixed pair: X alone 2.4 IPC, Y alone 1.0 IPC | 2.4 and 1.0 | 1.4 and 0.72 | 0.58 + 0.72 = **1.30** |
| two threads that each nearly saturate a 4-wide core (IPC 3.9 alone, for instance a dense FMA loop) | 3.9 each | 2.0 each | 0.51 + 0.51 = **1.03** |

The three rows give the rule of thumb: **SMT pays when a thread leaves the core idle** (cache misses, branches, dependence chains), pays little when a thread already uses the core's ports, and every thread runs slower than it would alone (0.58 and 0.72 of its solo speed in the middle row). SMT trades single-thread latency for throughput. A measured data point: a saturated HTTPS-serving benchmark on a 48-core AMD EPYC processor handled [41 to 43% more requests per second with SMT on than off, but its latency at the extreme tail got worse](https://blog.cloudflare.com/measuring-hyper-threading-and-turbo-boost/). Floating-point numerical codes that already saturate the vector units often gain nothing, and some run slower because the two threads split the L1 and L2 caches; HPC sites commonly run one thread per physical core.

**Is a logical CPU a core or a hardware thread?** This VM reports two cores with one thread each, but a hypervisor may present two hardware threads of one core as two "cores", so only a test tells. The test: run the same loop on one CPU and on both at once. *Measured* (median of three, seconds; each thread does the same fixed work):

| Loop | 1 thread | 2 threads at once |
|---|---|---|
| one dependent multiply-add chain (latency-bound) | 0.276 to 0.290 | 0.282 to 0.292 |
| four independent chains (saturates the multiplier port) | 0.069 to 0.076 | 0.071 to 0.076 |

Two threads took as long as one, for both loops. On two SMT siblings the second loop would have slowed down (they share the multiplier); here it did not, so **these two virtual CPUs behave as two separate cores**. You will use the same test in problem D.

### 3.5 SMT and security, in a paragraph

Two threads of one core share the L1 cache, the execution ports and the predictors, so a thread on one logical CPU can observe the cache and port usage of its sibling, and several speculative-execution leaks (the MDS family) work across siblings. Mitigations: **core scheduling** in the Linux kernel (only mutually trusting tasks share a core), turning SMT off (`nosmt` on the kernel command line, or `/sys/devices/system/cpu/smt/control`), and cloud providers that give a tenant whole cores. If you run untrusted code next to your own, count a core as one thread of capacity, not two. The attacks themselves are in [tutorial 30](30_hardware_security.md) (sections 2 and 4).

---

## 4. Machines with many cores

### 4.1 Shared-memory multiprocessors

In a **shared-memory** machine every core sees the same address space: one core stores to an address and another loads it. Each core has private L1 and L2 caches and shares the last-level cache and the memory system. Because the same line may sit in several private caches, hardware keeps them consistent (**cache coherence**, tutorial 19), and a separate set of rules says in what order other cores see the stores (**memory consistency**, tutorial 20).

If every core is equally far from every byte of memory the machine is **uniform memory access**, a **symmetric multiprocessor (SMP)**: a single-socket chip is close to this.

### 4.2 NUMA and distributed memory

| Organisation | Address spaces | Cost of a memory access | You program it with | Example |
|---|---|---|---|---|
| UMA / SMP | one | the same from every core | threads | a laptop, a one-socket server |
| cc-NUMA (cache-coherent non-uniform) | one, coherent | local fast; remote 1.3 to 2 times slower, with less bandwidth (tutorial 16 section 11) | threads, plus placement of threads and memory | a two-socket server |
| Distributed memory | one per node | remote data only by sending a message over a network, microseconds instead of nanoseconds | message passing (MPI), RPC, queues | a cluster, a data-centre service |

Hardware coherence gets more expensive as the core count grows (more caches to keep consistent, more traffic), so shared-memory machines are typically one to eight sockets (a few special designs go further) and anything bigger is a set of such machines linked by a network. The distributed-systems side of the programme is the same ideas with messages, failures and much larger latencies.

### 4.3 The uncore

A multicore chip is more than its cores. The rest, which Intel calls the **uncore** (AMD: the data fabric), contains the shared L3 (split into slices), the **interconnect** joining cores, L3 slices and memory controllers (a ring on small parts, a mesh on large servers; why the mesh wins is in [tutorial 29](29_interconnection_networks_and_warehouse_scale_computing.md) section 6), the memory controllers of tutorial 16, the PCIe and CXL root complex, the structures that track which cache holds which line (tutorial 19), and power management. Its design sets the L3 hit latency (higher on a big mesh than on a small ring), the bandwidth the cores share, and how traffic between cores flows. On this VM `lscpu` reports **260 MiB of L3 shared by the 2 virtual CPUs**: the guest sees the host chip's whole L3, which neighbours on the same host share.

### 4.4 Chiplets

Instead of one big die, many current processors are several small dies (**chiplets**) in one package: compute chiplets with a few cores and an L3 each, and a separate I/O chiplet with the memory controllers and PCIe.

- **Cost.** A large die is more likely to contain a defect. With a simple model, the fraction of good dies is *e*^(−*A* × *D*) for area *A* and defect density *D*. Take *D* = 0.1 defects per cm² (illustrative): a 600 mm² (6 cm²) die yields *e*^−0.6 = **55%**; a 150 mm² chiplet yields *e*^−0.15 = **86%**, and chiplets can be tested before they are assembled. Chiplets also allow one die to be reused across products and an old, cheap process for the I/O. [Tutorial 28](28_performance_and_power.md) section 1.4 returns to die cost, with a yield table.
- **Price.** A message between chiplets is slower and costs more energy than one on a die. AnandTech measured on a Ryzen 9 7950X a [core-to-core latency of about 15 to 19 ns between cores sharing an L3 and up to roughly 80 ns to a core on another chiplet](https://at-web1.www.anandtech.com/show/17585/amd-zen-4-ryzen-9-7950x-and-ryzen-5-7600x-review-retaking-the-high-end/10).

So a single socket can behave like a small NUMA machine. Keep threads that communicate in the same cache domain, check the topology with `lscpu -e` and `lstopo`, and know that firmware options (AMD "NPS", Intel "sub-NUMA clustering") can expose the domains as separate NUMA nodes (tutorial 16 section 11).

### 4.5 Heterogeneous cores

Section 1 applied inside the chip: a small core is the efficient way to do small work, and a big core is the fast way to do demanding work. Arm's **big.LITTLE** and DynamIQ clusters, Intel's **hybrid** parts (performance cores and efficient cores) and Apple's performance and efficiency cores all put both kinds on one chip. A little core has a fraction of the area, lower power, lower single-thread speed and usually no SMT.

**Worked example** (illustrative numbers): a big core gives performance 1.0 at power 1.0 and area 1.0. A little core gives 0.4 at power 0.1 and area 0.25. In the area of one big core, four little cores give 4 × 0.4 = **1.6** performance for 4 × 0.1 = **0.4** power: four times the performance per watt for parallel work, but a single thread runs at 0.4 of the speed.

The hard part is **placement**. The kernel must put heavy threads on big cores and light or background threads on little ones, and migrate them as behaviour changes:

- Linux has capacity-aware scheduling and **Energy Aware Scheduling (EAS)**, which uses an energy model of the cores to place tasks. According to the [kernel documentation](https://www.kernel.org/doc/html/v6.1/scheduler/sched-energy.html) it targets asymmetric-capacity systems, needs the `schedutil` frequency governor, is switched off when a CPU in the domain is more than 80% busy, and does not support SMT systems.
- Intel's hybrid parts give the OS hardware hints about each thread's behaviour (Thread Director); Apple's scheduler uses quality-of-service classes.
- For you: **pin** latency-critical threads to the big cores (`taskset -c`), and pin or control the cores when you benchmark, because a thread that migrates between a big and a little core changes speed in mid-run. `lscpu -e` shows different `MAXMHZ` per CPU, `/sys/devices/system/cpu/cpu*/cpu_capacity` shows Arm capacities, and on a Mac `sysctl hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu` gives the counts of performance and efficiency cores.

**Embedded note.** Wi-Fi access points and router SoCs usually have few cores (2 to 4, typically all of one kind) plus hardware engines that forward packets without the CPU. Which core services which interrupt (`/proc/irq/N/smp_affinity`), and keeping the control plane off the data-plane cores, matter far more there than SMT or chiplets.

---

## 5. Programming a multicore machine

### 5.1 Three programming models

| Model | What threads share | How they communicate | Typical tools |
|---|---|---|---|
| **Shared address space** | all memory | loads and stores, plus synchronisation (locks, atomics, barriers) | `pthread`/`std::thread`, OpenMP, thread pools |
| **Message passing** | nothing: each process owns its data | explicit send and receive, or collective operations | MPI, sockets and RPC, queues, Go channels, actors |
| **Data-parallel** | the same operation applies to every element of a collection | the structure of the operation (map, reduce) | SIMD instructions, GPU kernels, array libraries, map-reduce |

The same sum in each:

```c
/* shared address space (OpenMP): threads split the loop, partial sums are combined by the runtime */
#pragma omp parallel for reduction(+:sum)
for (long i = 0; i < n; i++) sum += a[i];

/* message passing (MPI): each rank sums its own slice, then one collective combines them */
MPI_Reduce(&local_sum, &sum, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);

/* data-parallel (GPU kernel): one thread per element, no loop in the code */
c[i] = a[i] + b[i];     // i = blockIdx.x * blockDim.x + threadIdx.x
```

Shared memory is the easiest to start and the easiest to get wrong (data races, deadlocks: tutorials 19 and 20). Message passing scales beyond one machine and makes communication visible and countable. Data-parallel code is simple and fast when the work is uniform. Real systems combine them: MPI between nodes, threads inside a node, SIMD inside a thread.

**Ways to divide the work:**

- **Data parallelism:** every worker does the same thing on a different part of the data (split an array, a packet batch, a hash table into shards).
- **Task parallelism:** different workers do different jobs (decode, compress, upload).
- **Pipeline parallelism:** stages connected by queues, each stage on its own thread. Throughput is set by the slowest stage; latency is the sum of the stages.

**Worked example (a pipeline).** Three stages of a packet path take 10, 25 and 15 µs per item. Throughput is 1 / 25 µs = **40,000 items/s**, set by the slow stage. Give the slow stage two workers: it now produces an item every 12.5 µs, the 15 µs stage becomes the bottleneck, and throughput rises to 1 / 15 µs = **66,700 items/s**, 1.67 times as much. A third worker on stage 2 gains nothing (the bottleneck stays at 15 µs). Find the bottleneck first, then widen only it.

### 5.2 Work distribution and load balance

With *N* threads and a pile of tasks, the run takes as long as the slowest thread (the **makespan**). Three ways to hand out tasks:

| Method | Rule | Cost | Weakness |
|---|---|---|---|
| **Static block** | thread *t* gets the *t*-th contiguous range | none | all load balance rests on the tasks costing the same |
| **Static cyclic** | thread *t* gets tasks *t*, *t* + *N*, *t* + 2*N*… | none | can still be unlucky if the costs have a period |
| **Dynamic** (self-scheduling) | each idle thread takes the next task (or chunk) from a shared counter | one atomic operation per task (section 6.4: 7 ns uncontended, more when contended) | the counter is a shared hot spot; chunks must be large enough |
| **Work stealing** | each thread has its own deque; an idle thread steals from another | cheap when balanced, a transfer only when needed | more complex; used by Cilk, TBB, Rust's rayon, Go and Java fork-join |

**Worked example.** 64 tasks: the first 56 cost 1 unit and the last 8 cost 10 units each, total 56 + 80 = **136 units**, on 2 threads. The ideal makespan is 68.

- **Static block:** thread 0 gets tasks 0 to 31 (32 units), thread 1 gets tasks 32 to 63 (24 + 80 = 104 units). Makespan **104**, speedup 136 / 104 = **1.31**, efficiency 65%. One thread works while the other waits.
- **Static cyclic:** thread 0 gets the even tasks and thread 1 the odd ones: each gets 28 light tasks and 4 heavy ones, 68 units. Speedup **2.0**: lucky, because the heavy tasks alternate evenly.
- **Dynamic:** the threads finish the 56 light tasks at time 28 each, then take the heavy ones four apiece: 28 + 40 = 68. Speedup **2.0**, and the same if the heavy tasks come first. In general a dynamic schedule finishes within (total / *N*) + (*N* − 1) / *N* × (the largest task) of the ideal, here 68 + 5 = 73 at worst.

*Measured* on the two virtual CPUs (the same 64 tasks, one unit = 0.4 ms of work; speedup over one thread, range over three runs):

| Schedule | Speedup | Predicted |
|---|---|---|
| static block (heavy tasks last) | 1.10 to 1.31 | 1.31 |
| static cyclic | 1.88 to 2.02 | 2.0 |
| dynamic, one task at a time | 1.86 to 2.00 | 2.0 |
| dynamic, heavy tasks first | 1.92 to 1.99 | 2.0 |

The static block schedule gives about the predicted 1.3 times (one run came out lower), the others reach about 2. **Rule: if the cost per task varies or is unknown, schedule dynamically with chunks big enough to hide the counter, and put the big tasks first.** If the costs are uniform and known, static is free and keeps each thread's data in its own cache.

### 5.3 Granularity: do not start a thread for a small job

Starting a thread has a cost, so a parallel region must contain enough work. *Measured*: creating and joining two empty threads takes **53 to 68 µs** (median). If the serial job takes time *T* and two threads each take *T* / 2, the parallel version takes *T* / 2 + *S* for a start-up cost *S*, which is faster only when **T > 2S**: more than about 105 to 135 µs of work. Summing *n* integers (0.66 ns per element, measured):

| Elements *n* | Serial | 2 threads started each time | Speedup |
|---|---|---|---|
| 1,000 | 0.5 to 1.3 µs | 54 to 69 µs | 0.01 to 0.02 |
| 10,000 | 5 to 10 µs | 54 to 66 µs | 0.08 to 0.18 |
| 100,000 | 45 to 64 µs | 77 to 101 µs | 0.51 to 0.83 |
| 1,000,000 | 656 to 695 µs | 355 to 395 µs | 1.68 to 1.96 |

The break-even by the formula is *n* = 2 × 55 µs / 0.66 ns ≈ 170,000 elements, between the rows for 100,000 (a loss) and 1,000,000 (a win). Parallel code on small inputs is slower than serial code. The usual fix is a **thread pool**: the threads are created once, stay alive, and wake up for each job, which takes microseconds (a spinning worker responds in well under one, at the price of a busy core) instead of tens of microseconds. Even so, hand each worker a chunk that takes much longer than the hand-off.

---

## 6. Scaling limits

### 6.1 Amdahl's law

If a fraction *s* of a program's single-thread time is **serial** (cannot be sped up by more cores) and 1 − *s* is perfectly parallel, then on *N* cores

```text
T(N) = T(1) × ( s + (1 − s) / N )            Speedup S(N) = 1 / ( s + (1 − s) / N )        limit as N grows: 1 / s
Efficiency = S(N) / N
```

"Serial" means anything that does not get faster with more cores: initialisation, reading input, code inside one lock, the merge of partial results, and the thread that feeds the others.

| Cores *N* | Speedup, *s* = 5% | Speedup, *s* = 10% |
|---|---|---|
| 2 | 1.90 | 1.82 |
| 4 | 3.48 | 3.08 |
| 8 | 5.93 | 4.71 |
| 16 | 9.14 | 6.40 |
| 64 | 15.4 | 8.77 |
| ∞ | **20** | **10** |

With only 5% serial code, 16 cores are 57% efficient and 64 cores give a speedup of 15 for 64 times the hardware. The serial fraction, not the number of cores, decides the ceiling.

*Measured* check: a program of fixed size whose serial fraction is set by construction (part of the work in a loop run by one thread, the rest split over two threads), 2 threads, range over three runs:

| Serial fraction *s* | Predicted speedup | Measured |
|---|---|---|
| 0 | 2.00 | 1.90 to 2.00 |
| 0.10 | 1.82 | 1.74 to 1.82 |
| 0.25 | 1.60 | 1.57 to 1.61 |
| 0.50 | 1.33 | 1.25 to 1.32 |

The formula predicts the real runs within a few percent (the measured values at the high-*s* end are a little lower: starting the threads costs about 55 µs and a one-thread phase can run at a higher clock).

### 6.2 Gustafson's law: scale the problem

Amdahl assumes the problem stays the same size (**strong scaling**: the same job, faster). If you instead use a bigger machine to solve a bigger problem, so that the parallel part grows with *N* while the serial part stays fixed (**weak scaling**: the same time per core), the scaled speedup is

```text
S_scaled(N) = N − s × (N − 1)          s = serial fraction measured on the N-core run
```

For *s* = 5% and *N* = 16: 16 − 0.05 × 15 = **15.25**, against Amdahl's 9.14 for the fixed problem. The laws do not contradict each other; they answer different questions. A request-serving farm adds machines to serve more users (weak scaling); a latency-critical job that must finish sooner is a strong-scaling problem and Amdahl applies.

### 6.3 Karp-Flatt: find the serial fraction from a measurement

Given a measured speedup *S* on *N* cores, the **experimentally determined serial fraction** is

```text
e = ( 1/S − 1/N ) / ( 1 − 1/N )
```

If *e* stays constant as *N* grows, the limit is plain Amdahl: a serial part of size *e*. If *e* **grows with N**, something costs more as you add cores: synchronisation, communication, memory bandwidth, imbalance.

**Worked example.** Measured speedups on 2, 4 and 8 cores are 1.9, 3.5 and 6.0. Then *e* = (1/1.9 − 1/2) / (1/2) = **0.053**; (1/3.5 − 1/4) / (3/4) = **0.048**; (1/6 − 1/8) / (7/8) = **0.048**. Constant: about 5% serial work; look for the serial code. From the two-thread runs of section 6.1, a speedup of 1.57 to 1.61 gives *e* = 0.27 to 0.24, close to the 0.25 that the program was built with.

### 6.4 What else limits scaling

Amdahl is the optimistic bound. Real programs also lose speed to the following, and each shows up as *e* that grows with *N*.

**1. Contention on shared data.** *Measured*: 20 million increments of one shared counter with an atomic add (`atomic_fetch_add`), split over the threads:

| | Time |
|---|---|
| 1 thread | 0.131 to 0.141 s (6.6 to 7.1 ns per increment) |
| **2 threads, one shared counter** | **0.297 to 0.330 s** |
| 1 or 2 threads, each with its own counter, added at the end | 0.004 to 0.014 s |

Two threads on one atomic counter were **2.2 to 2.5 times slower than one thread** (speedup 0.4 to 0.45, below 1). Each increment needs exclusive ownership of the cache line, so the line ping-pongs between the two cores; tutorial 19 explains how and what it costs. The fix is structural: **give each thread its own data and combine at the end** (per-thread counters, sharded maps, per-core queues).

**2. Memory bandwidth.** A loop that streams memory scales until the memory system is saturated, then stops. By Little's law (tutorial 16 section 7) one core can keep only about 10 to 16 lines in flight, which is roughly 8 to 12 GB/s, while one DDR5-4800 channel supplies 38.4 GB/s and an 8-channel socket 307 GB/s: about 4 or 5 cores saturate a channel and about 29 cores 80% of the socket (tutorial 16, problem B). *Measured*, summing a 1 GiB array on this VM: 1 thread **8.2 to 9.0 GB/s**, 2 threads **15.1 to 17.3 GB/s** (1.73 to 1.93 times). Two cores are still far from the host's limit, so the curve is still rising; on a laptop it flattens after a few cores. A bandwidth-bound loop's speedup is min(*N*, peak bandwidth ÷ per-core bandwidth), and the cure is to move fewer bytes (tutorial 14), not to add threads.

**3. Shared cache and the frequency budget.** The threads share the L3, so each has less of it (the working set per thread must still fit), and the all-core clock is lower than the single-core clock (section 1.3).

**4. Imbalance and granularity.** Sections 5.2 and 5.3.

**5. Oversubscription.** More runnable threads than CPUs means the OS time-slices them: a context switch costs microseconds and the caches go cold, with no added compute. *Measured*, the OpenMP sum of problem D on 2 virtual CPUs (1 GiB of integers, range over several runs): 1 thread 0.21 to 0.27 s, 2 threads 0.11 to 0.13 s, 4 threads 0.11 to 0.12 s, so four threads were no faster than two. For CPU-bound work use about one thread per core. For work that blocks on I/O, a common sizing rule is *threads = cores × (1 + wait time ÷ compute time)*: 8 cores, 50 ms waiting and 5 ms computing per request gives 8 × (1 + 10) = **88 threads**.

**6. Placement.** Remote NUMA memory, another chiplet's L3 or a little core (sections 4.2 to 4.5).

A speedup curve therefore **rises, bends (Amdahl), flattens (bandwidth) and can turn down (contention or oversubscription)**. When asked to explain a scaling result, find which of these six it is, using *e*, `perf stat` (cycles, cache misses) and the memory-bandwidth counters.

---

## 7. Looking at your machine

| Question | Linux | macOS |
|---|---|---|
| How many logical CPUs, cores, sockets, threads per core? | `nproc`, `lscpu` | `sysctl hw.physicalcpu hw.logicalcpu` |
| Which CPUs are siblings, and which share a cache or a NUMA node? | `lscpu -e`, `lstopo` (hwloc), `cat /sys/devices/system/cpu/cpu0/topology/thread_siblings_list` | (not exposed) |
| Is SMT on? | `cat /sys/devices/system/cpu/smt/active` | `hw.logicalcpu` greater than `hw.physicalcpu` |
| Performance and efficiency cores? | `lscpu -e` (different `MAXMHZ`), `/sys/devices/system/cpu/cpu*/cpu_capacity` on Arm | `sysctl hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu` |
| NUMA nodes and distances? | `numactl -H`, `lscpu` | one node |
| Run on chosen CPUs? | `taskset -c 0,2 ./prog`, `numactl --cpunodebind` | (no pinning API; QoS classes only) |

Two traps. In a container or a cloud VM, `std::thread::hardware_concurrency()` (C++), `os.cpu_count()` (Python) and OpenMP's default report the **logical CPUs of the host or VM**, not the CPU quota the container may get, so a runtime that sizes its thread pool from them can start more threads than the quota allows and get throttled. And a virtual machine's topology is whatever the hypervisor says; only a test like the one in section 3.4 shows whether two CPUs are SMT siblings.

---

## 8. What this means for your code

| Symptom | Likely cause | Try |
|---|---|---|
| Two threads are slower than one | contention on one lock, counter or cache line; or too little work per thread (start-up cost) | per-thread data merged at the end (tutorial 19); bigger chunks; a thread pool |
| The speedup flattens at 4 to 8 cores on a scan or a copy | memory bandwidth (Little's law, tutorial 16) | move fewer bytes: blocking, narrower types, fusion, compression; do not add threads |
| The speedup flattens early although every core is busy | a serial section (a lock, I/O, the merge) | compute the Karp-Flatt *e*; profile the serial part; shard the lock |
| One thread finishes long after the others | static split of uneven work | dynamic scheduling or work stealing, big tasks first |
| Throughput rose but the p99 latency got worse after turning SMT on | sibling threads compete for the core | measure the tail with SMT off; give latency-critical threads a whole core |
| Benchmark times jump from run to run on a laptop or a phone | migration between big and little cores, turbo state, thermal limits | pin with `taskset`, fix the governor, repeat and report percentiles |
| A CPU-bound service with 4 times more threads than cores is no faster | oversubscription | about one thread per core; use asynchronous I/O for waiting |
| Threads that talk to each other are slow on a big server | they sit on different chiplets or sockets | place them in one cache domain or NUMA node (`lscpu -e`, `lstopo`, `numactl`) |
| A containerised program starts too many threads and is throttled | `hardware_concurrency()` reports the host's CPUs, not the CPU quota | size pools from the cgroup quota, or set them explicitly |
| Untrusted code runs on the sibling thread of yours | SMT side channels | core scheduling, `nosmt`, or whole-core allocation per tenant |
| A router or access-point data path saturates one core while others idle | one queue and one interrupt on one core | spread flows over queues and cores, set interrupt affinity, use hardware offload; keep the control plane off the data-plane cores |

---

## 9. Mistakes that cost points

1. **Calling a superscalar out-of-order core SIMD or MIMD.** It presents one instruction stream: SISD. The parallelism inside it is hidden.
2. **Counting logical CPUs as cores.** An SMT thread is worth 1.0 to about 1.4 of a core, not 2.0, and a 16-vCPU VM may be 8 cores. Check `lscpu`.
3. **Quoting the cube law everywhere.** *P* ∝ *f*³ holds only while the voltage can follow the frequency; below the voltage floor power falls only linearly and leakage does not fall at all.
4. **Using Amdahl's law with the wrong serial fraction.** Measure it, count locks, I/O and merges, and remember it assumes a fixed problem size; Gustafson answers the scaled problem.
5. **Expecting 2 times from 2 threads on a memory-bound loop,** or on a loop with too little work: bandwidth and start-up cost cap it.
6. **Starting threads for small jobs.** A start costs 50 µs or more; use a pool and big chunks.
7. **Splitting uneven work statically.** The slowest thread decides the finish time.
8. **One shared counter or lock updated by every thread.** The speedup can fall below 1.
9. **More threads than cores for CPU-bound work.** It adds context switches and cold caches, not speed.
10. **Benchmarking a heterogeneous or turbo machine without controlling it.** Pin the threads, note the frequency, repeat.
11. **Treating a chiplet or multi-socket CPU as uniform.** Placement matters (section 4).
12. **Confusing concurrency with parallelism.** Many threads taking turns on one core are concurrent, not parallel.

---

## 10. Questions you will be asked

**Why did processors move to multiple cores?** Dynamic power is α*CV*²*f* and the voltage must rise with the frequency, so power grows about as *f*³. Two cores at 0.8 of the frequency give 1.6 times the (ideal) performance for the power of one; one core at 1.6 times the frequency costs 4.1 times the power. Voltage cannot keep scaling down, one bigger core finds little more parallelism, so the transistors go into cores, caches and engines.

**What are Flynn's classes and where do multicore CPUs, vector units and GPUs fit?** SISD (one stream, one data item), SIMD (one instruction, many data), MISD (rare), MIMD (independent streams). A multicore CPU is MIMD; a vector instruction is SIMD; a GPU runs SIMD groups (warps) scheduled MIMD-style over many multiprocessors (SIMT).

**What is SMT, and how does it differ from having two cores?** A core keeps the state of two (or more) threads and fills its issue slots from both in the same cycle. It duplicates only the registers and program counter and shares the execution units and caches, so it gains 0 to about 40% in throughput, depending on how much of the core one thread leaves idle, and each thread runs slower than alone. A second core doubles everything.

**When does SMT hurt?** When both threads saturate the same ports or thrash the same caches; when tail latency matters (siblings interfere); when per-core software licences are paid; and on multi-tenant hosts, where sibling threads can leak data through shared caches and ports.

**Compare coarse-grained, fine-grained and simultaneous multithreading.** Coarse: switch thread on a long stall. Fine: a different thread each cycle, round robin over ready threads (GPUs). Simultaneous: slots from several threads in the same cycle, on a wide out-of-order core.

**UMA, NUMA and distributed memory?** UMA: one address space, equal access cost. cc-NUMA: one address space but local memory is faster than remote, so placement matters. Distributed: separate address spaces, communication by messages.

**Name the parallel programming models.** Shared address space (threads, OpenMP), message passing (MPI, RPC, channels), data-parallel (SIMD, GPU kernels, map-reduce); work divided by data, by task or by pipeline stage.

**State Amdahl's law and use it.** *S* = 1 / (*s* + (1 − *s*) / *N*). With 5% serial code, 16 cores give 9.1 times and the limit is 20 times; the serial fraction sets the ceiling. It assumes a fixed problem size; Gustafson's law, *S* = *N* − *s*(*N* − 1), covers a problem that grows with the machine.

**Why might 8 threads be slower than 1?** Contention (a lock or a shared cache line), start-up cost for tiny jobs, memory-bandwidth saturation, oversubscription, or false sharing (tutorial 19). Karp-Flatt (*e* growing with *N*) tells you it is overhead and not a serial fraction.

**How do you divide uneven work among threads?** Dynamic scheduling (a shared counter hands out chunks) or work stealing (per-thread deques), biggest tasks first; static only when costs are uniform and known.

**How many threads should a service use?** CPU-bound: about one per core (not per logical CPU if SMT hurts the tail). Blocking on I/O: cores × (1 + wait ÷ compute), or use asynchronous I/O and keep to the core count.

**What is the uncore, and why do chiplets change software?** The uncore is the shared L3, the interconnect, the memory controllers and the I/O. Chiplets cut cost (small dies yield better) but cross-chiplet communication is several times slower than within one, so threads that share data should share a cache domain.

**What is big.LITTLE (or P-cores and E-cores), and how is it scheduled?** Cores of different size on one chip: small ones for light or parallel work at low energy, big ones for demanding threads. The kernel places threads by capacity and with an energy model (EAS on Linux); a benchmark should pin its threads.

**Strong scaling versus weak scaling?** Strong: a fixed problem on more cores (Amdahl). Weak: a problem that grows with the number of cores, constant work per core (Gustafson).

---

## 11. Problems

**How to use this section.** Try each problem before you read the answers. A to C are paper problems (25 to 30 minutes each); D is paper plus a short program on your own machine.

### Problem A (paper): power and frequency

(a) A core draws 15 W at 3.0 GHz. Using the cube law, what does it draw at 2.4 GHz and at 4.0 GHz?
(b) Replace it with four cores at 1.5 GHz each. Ideal speedup, power and performance per watt, relative to the single core.
(c) The job is 30% serial. What is the real speedup of the four slow cores over the single 3.0 GHz core, the power, and the energy used for the whole job (power × time)? Repeat for a job that is 10% serial.
(d) Why does the cube law stop holding at low frequency, and what does that do to the idea of ever more, ever slower cores?

*Where:* paper. *How:* section 1. Time for the slow design = (time at 3.0 GHz) × (3.0 / 1.5) × (*s* + (1 − *s*) / 4).

### Problem B (paper): Flynn and SMT

(a) Classify each as SISD, SIMD, SIMT or MIMD: (1) one thread of scalar code on an out-of-order core; (2) a `vaddps` that adds 8 floats; (3) a GPU kernel on a block of 256 threads; (4) a 64-core server running 64 independent request handlers; (5) a 100-node cluster of MPI ranks. Which of them has distributed memory?
(b) Weighted speedup of SMT for three pairs of threads, given IPC alone → together: (i) 2.0 → 1.2 and 0.8 → 0.6; (ii) 3.8 → 1.95 and 3.8 → 1.95; (iii) 0.3 → 0.28 and 0.3 → 0.28. In which cases would you enable SMT?
(c) A service on 16 cores with SMT (32 logical CPUs) shows +30% throughput with SMT in a saturating benchmark. In production it runs at 40% load with a p99.9 latency target. Should SMT be on? What would you measure?

*Where:* paper. *How:* sections 2 and 3. Weighted speedup = Σ (IPC together ÷ IPC alone); 1.0 means no gain.

### Problem C (paper): Amdahl, Gustafson and Karp-Flatt

(a) A job is 92% parallel. Speedup on 4, 16 and 64 cores; the limit; the efficiency at 16 cores.
(b) How many cores give 80% of the limit?
(c) The serial fraction measured on a 16-core run of the scaled problem is 8%. What does Gustafson's law give, and why is it higher than Amdahl's 16-core figure?
(d) Two sets of measured speedups on 2, 4 and 8 cores: set 1 is 1.8, 3.1, 4.8; set 2 is 1.8, 2.9, 3.6. Compute the Karp-Flatt *e* for each. Which one is limited by a serial fraction and which by growing overhead? What would you check for each?
(e) A 16-core part runs one active core at 5.0 GHz and all 16 at 4.0 GHz. A program is 5% serial (by time on one core at 5.0 GHz). Its serial part runs on one core at 5.0 GHz, its parallel part on 16 cores at 4.0 GHz. What is the speedup over a single thread at 5.0 GHz?

*Where:* paper. *How:* section 6. For (e), write the time in units of the single-thread time.

### Problem D (paper, then your machine): scheduling, granularity and measurement

(a) Sixteen tasks cost 9, 7, 6, 5, 4, 4, 3, 3, 2, 2, 1, 1, 1, 1, 1, 1 units (total 51). On 4 threads give the makespan and speedup for: static block (four consecutive tasks per thread), static cyclic (task *i* to thread *i* mod 4), dynamic in the order listed, dynamic in the reverse order. Compare with the ideal.
(b) Starting four threads costs 80 µs in total, and summing one element costs 0.66 ns. For what number of elements does a 4-thread sum beat the serial sum?
(c) On the test VM 20 million increments took 0.131 s with one thread and about 0.30 s with two threads sharing one atomic counter. Give the total rate in increments per second in each case. What total rate can 64 threads on the same counter reach at best, and what design avoids the problem?
(d) *On your machine:* find the logical CPUs, cores and threads per core (section 7). Then compile and run the program below, which sums a 1 GiB array with 1, 2, 4… threads up to the number of logical CPUs. Record the times and compute the speedup and the Karp-Flatt *e* at each count. Where does the curve bend or flatten, and which limit from section 6.4 is it? If your machine has SMT, compare the time with one thread per core against all logical CPUs.

```c
/* sum.c   Linux: gcc -O2 -fopenmp sum.c -o sum
   macOS:  brew install libomp, then
           clang -O2 -Xpreprocessor -fopenmp -I$(brew --prefix libomp)/include \
                 -L$(brew --prefix libomp)/lib -lomp sum.c -o sum              */
#include <stdio.h>
#include <stdlib.h>
#include <omp.h>
int main(void) {
    long n = 1L << 28;                          /* 256M ints = 1 GiB */
    int *a = malloc(n * sizeof *a);
    for (long i = 0; i < n; i++) a[i] = 1;
    int maxt = omp_get_max_threads();           /* read it once: set_num_threads changes it */
    for (int t = 1; t <= maxt; t *= 2) {
        omp_set_num_threads(t);
        double t0 = omp_get_wtime();
        long s = 0;
        #pragma omp parallel for reduction(+:s)
        for (long i = 0; i < n; i++) s += a[i];
        printf("%2d threads: %.3f s  (sum %ld)\n", t, omp_get_wtime() - t0, s);
    }
    return 0;
}
```

*Where:* paper for (a) to (c); a terminal for (d). *How:* sections 5 and 6. List scheduling: give the next task to the thread that becomes free first. Run (d) three times and use the median; close other programs.

---

## Answers

### Problem A

(a) Power scales with the cube of the frequency. At 2.4 GHz: 15 × (2.4 / 3.0)³ = 15 × 0.512 = **7.68 W**. At 4.0 GHz: 15 × (4 / 3)³ = 15 × 2.37 = **35.6 W**. A third more clock costs 2.4 times the power; a fifth less clock saves half of it.

(b) Ideal speedup 4 × 0.5 = **2.0**. Power 4 × 0.5³ = **0.5** (7.5 W). Performance per watt 2.0 / 0.5 = **4 times**.

(c) For *s* = 0.3 the time is 2 × (0.3 + 0.7 / 4) = 2 × 0.475 = **0.95** of the original: a speedup of only **1.05**. Power 0.5, so energy = 0.5 × 0.95 = **0.475**: the same job in about half the energy at the same speed. For *s* = 0.1 the time is 2 × (0.1 + 0.9 / 4) = 0.65, speedup **1.54**, energy 0.5 × 0.65 = **0.325**. (For *s* = 0 the speedup is 2.0 and the energy 0.25.) The slow, wide design is always better on energy; whether it is faster depends on the serial fraction.

(d) The cube law needs the voltage to fall with the frequency. At some voltage the transistors stop switching reliably (the floor); below that frequency the power falls only linearly with *f*, and leakage stays constant, so energy per operation stops improving and going slower gains little. The practical answer is a balance: more cores only while the software can use them and while the voltage can still fall, then specialised hardware, and power-gating idle cores.

### Problem B

(a) (1) **SISD** (an out-of-order core still presents one instruction stream). (2) **SIMD** (one instruction, 8 data items). (3) **SIMT**: the block runs as 8 warps of 32 threads, each warp executing one instruction for 32 threads, and the warps are scheduled independently across the multiprocessor. (4) **MIMD** with shared memory. (5) **MIMD** with **distributed memory**, the only one of the five; its ranks communicate by messages.

(b) (i) 1.2 / 2.0 + 0.6 / 0.8 = 0.60 + 0.75 = **1.35**. (ii) 1.95 / 3.8 × 2 = **1.03**. (iii) 0.28 / 0.3 × 2 = **1.87**. Enable SMT for (i) and (iii): the threads leave the core idle, and (iii), two latency-bound threads, is nearly free. In (ii) there is nothing to gain: both threads need the whole core, each runs at half speed, and there is only the risk of a worse tail latency and cache contention.

(c) Probably test before enabling. At 40% load the machine is not saturated, so the +30% that matters at saturation is mostly not needed, while the extra tail latency from sibling interference applies all the time. Measure p99 and p99.9 with SMT on and off at the production load and at peak, compare the cost of the extra servers needed with SMT off, and consider the compromise: SMT on, but latency-critical threads on a whole core (both siblings reserved). Also check the security requirement for co-tenants.

### Problem C

(a) *s* = 0.08. *N* = 4: 1 / (0.08 + 0.92 / 4) = 1 / 0.31 = **3.23**. *N* = 16: 1 / (0.08 + 0.0575) = **7.27**. *N* = 64: 1 / (0.08 + 0.0144) = **10.6**. Limit 1 / 0.08 = **12.5**. Efficiency at 16 cores: 7.27 / 16 = **45%**.

(b) 80% of 12.5 is 10.0: 1 / (0.08 + 0.92 / *N*) = 10 gives 0.08 + 0.92 / *N* = 0.1, so *N* = 0.92 / 0.02 = **46 cores**. The last 20% of the limit costs more than 46 cores.

(c) 16 − 0.08 × 15 = **14.8**. Amdahl's 7.27 is for a problem of fixed size, which becomes a smaller and smaller share of the machine's time. Gustafson's is for a problem that grows with the cores, so the parallel part keeps all 16 cores busy and the serial part is a smaller share of the (longer) run. They answer different questions.

(d) *e* = (1/*S* − 1/*N*) / (1 − 1/*N*). Set 1: *N* = 2: (0.5556 − 0.5) / 0.5 = **0.111**; *N* = 4: (0.3226 − 0.25) / 0.75 = **0.097**; *N* = 8: (0.2083 − 0.125) / 0.875 = **0.095**. Constant at about 10%: a serial fraction (Amdahl); look for the serial code, a lock, the reduction. Set 2: **0.111**, (0.3448 − 0.25) / 0.75 = **0.126**, (0.2778 − 0.125) / 0.875 = **0.175**. *e* grows with the cores, so a cost that rises with *N*: synchronisation, memory bandwidth, imbalance or a contended line. Check the memory-bandwidth counters, `perf` for cache-to-cache transfers (tutorial 19) and the load balance.

(e) Time relative to one thread at 5.0 GHz: serial part 0.05; parallel part 0.95 on 16 cores at 4.0 GHz = 0.95 / 16 × (5.0 / 4.0) = 0.0742. Total **0.1242**, a speedup of **8.05**, against 9.14 for equal clocks.

### Problem D

(a) The total is 51; the ideal is 51 / 4 = 12.75 units.
- **Static block**: the threads get 9 + 7 + 6 + 5 = 27; 4 + 4 + 3 + 3 = 14; 2 + 2 + 1 + 1 = 6; 1 + 1 + 1 + 1 = 4. Makespan **27**, speedup 51 / 27 = **1.89**.
- **Static cyclic**: thread 0 gets 9, 4, 2, 1 = 16; thread 1 gets 7, 4, 2, 1 = 14; thread 2 gets 6, 3, 1, 1 = 11; thread 3 gets 5, 3, 1, 1 = 10. Makespan **16**, speedup **3.19**.
- **Dynamic, listed order** (biggest first): the first four go to the four threads (9, 7, 6, 5); then each next task goes to the thread that becomes free first. Final loads 13, 13, 13, 12. Makespan **13**, speedup **3.92**, within 2% of the ideal.
- **Dynamic, reverse order** (small tasks first, the 9 last): the small tasks pile up evenly, then the large ones arrive at the end. Makespan **16**, speedup **3.19**. (The guaranteed bound for any order is 51 / 4 + 9 × 3 / 4 = 19.5.) So dynamic scheduling is only as good as its order: start with the big tasks.

(b) With *c* = 0.66 ns per element, the serial time is *n* × *c*. Four threads take *n* × *c* / 4 + 80 µs. They win when *n* × *c* × (3/4) > 80 µs, so *n* > 80 µs / (0.75 × 0.66 ns) = **about 160,000 elements**.

(c) One thread: 20 × 10⁶ / 0.131 s = **153 million increments per second**. Two threads on one counter: 20 × 10⁶ / 0.30 s = **67 million per second**, less than half the single-thread rate. A single contended counter handles about one increment per cache-line hand-off however many threads there are, so 64 threads can at best reach about the same **67 million per second** (usually less), and may be worse. The cure is a design change: one counter per thread (each at about 150 million per second, 64 threads about 9.8 billion per second in total, if nothing else limits), and add them up when the value is needed.

(d) What to expect on a typical machine: close to linear scaling at the start, and a flattening at 3 to 8 threads for this summing loop because it is bandwidth-bound (a single core streams roughly 8 to 12 GB/s while a laptop memory system supplies tens of GB/s; section 6.4); an extra bump or a plateau when the thread count goes beyond the physical cores onto SMT siblings, usually small for a loop that is limited by memory. A constant *e* would point to a serial part, but this program has almost none (the initialisation is outside the timed region), so a growing *e* points to bandwidth. On the test VM: 1 thread 0.21 to 0.27 s, 2 threads 0.11 to 0.13 s, 4 threads 0.11 to 0.12 s (two CPUs only).

---

## Cheat sheet

- **Power:** *P*dyn = α*CV*²*f*; *V* rises with *f*, so *P* ∝ *f*³ over the usable range. *N* cores at *f*: ideal performance *N*·*f*, power *N*·*f*³. Voltage floor, leakage, dark silicon; all-core clock below single-core clock.
- **Flynn:** SISD, SIMD, MISD (rare), MIMD. SPMD is the way MIMD is programmed. GPU = SIMT. Out-of-order superscalar = SISD.
- **Multithreading:** coarse (switch on a long stall), fine (a ready thread every cycle: GPUs), SMT (slots from several threads in a cycle: Intel and AMD 2, POWER 4 or 8; most Arm and Apple cores none). Duplicated: PC, registers, rename map. Shared: ports, caches, predictors, queues. Weighted speedup = Σ IPC_together ÷ IPC_alone: 1.9 latency-bound, 1.3 typical, 1.0 saturated; each thread slower; the tail can get worse; side-channel risk (core scheduling, `nosmt`).
- **Memory organisation:** UMA/SMP; cc-NUMA (remote 1.3 to 2× local); distributed (messages). Coherence = tutorial 19, consistency = tutorial 20. The uncore: L3, interconnect, memory controllers, I/O.
- **Chiplets:** better yield (*e*^−*AD*: 55% for 600 mm² vs 86% for 150 mm² at 0.1/cm²) and reuse; cross-chiplet latency 15 to 19 ns inside an L3 domain vs up to about 80 ns across (Zen 4 desktop). Heterogeneous: big plus little cores, EAS and capacity-aware scheduling, pin when benchmarking.
- **Models:** shared address space, message passing, data-parallel; data, task and pipeline decomposition (pipeline throughput = 1 ÷ slowest stage).
- **Load balance:** static block, cyclic, dynamic, work stealing; biggest first; list scheduling ≤ total/*N* + (*N* − 1)/*N* · t_max. Measured (2 threads): block 1.1 to 1.3, cyclic and dynamic 1.9 to 2.0.
- **Granularity:** thread start + join 53 to 68 µs here; parallel pays only when serial time > 2 × start-up cost; use pools and big chunks.
- **Amdahl:** *S* = 1 / (*s* + (1 − *s*)/*N*), limit 1/*s* (5% → 20×; 16 cores → 9.1×). Gustafson: *S* = *N* − *s*(*N* − 1). Karp-Flatt: *e* = (1/*S* − 1/*N*) / (1 − 1/*N*); constant = serial fraction, growing = overhead.
- **Other limits:** contention (one shared atomic: 2 threads 2.2 to 2.5× slower than 1), memory bandwidth (speedup ≤ peak ÷ per-core bandwidth; 4 to 5 cores per DDR5 channel), shared cache, lower all-core clock, imbalance, oversubscription (4 threads on 2 CPUs = no gain). Threads for I/O-bound work: cores × (1 + wait ÷ compute).
- **Tools:** `lscpu`, `lscpu -e`, `lstopo`, `nproc`, `taskset`, `numactl -H`, `/sys/devices/system/cpu/smt/active`, `sysctl hw.perflevel0.physicalcpu`; `hardware_concurrency()` ignores container quotas.

**Next:** tutorial 19, Cache Coherence (the coherence problem, snooping protocols MSI, MESI, MOESI, directory protocols, false sharing and cache-line bouncing, the true cost of an atomic, `perf c2c`, lock and barrier scalability). Say "next" when you are ready.
