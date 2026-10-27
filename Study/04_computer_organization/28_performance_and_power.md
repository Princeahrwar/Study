# CO 28: Performance and Power

*Part of the [Computer Organization plan](00_plan.md) · Core tutorial · Time: about 5 hours (3 reading, 2 problems) · You need: [tutorial 01](01_how_a_computer_works.md) (section 9: the iron law), [tutorial 12](12_inside_a_modern_core.md) (sections 9 and 10: loop bounds and top-down), [tutorial 14](14_cache_optimisation_and_the_real_hierarchy.md) (section 3: memory-level parallelism), [tutorial 18](18_multicore_and_multithreading.md) (sections 1 and 6: the power wall, Amdahl's law)*

By the end of this tutorial you can:

- say why single-thread speed stopped doubling (Dennard scaling, the power wall, dark silicon) and what die size and yield do to chip cost;
- use the iron law with real numbers: find the clock a core is *really* running at, derive IPC from a time, and say why a timestamp counter is not a cycle counter;
- summarise benchmark results correctly (geometric and harmonic means, SPEC CPU, MLPerf) and see through MIPS, peak FLOPS and unlabelled speedups;
- write a microbenchmark that measures what you think it does, and recognise the traps that make it lie;
- apply Amdahl's law to optimisation choices, the Universal Scalability Law to thread scaling, and Little's law and a simple queue to capacity;
- explain latency against throughput, percentiles, tail amplification by fan-out, and coordinated omission;
- build a roofline for a CPU from two measured numbers and place real kernels on it;
- break time into a CPI stack, use `perf` and flame graphs, and follow a measurement method;
- compute dynamic and static power, find the energy-optimal frequency in a model, and explain P-states, C-states, turbo, power limits, clock and power gating, and performance per watt.

Every earlier tutorial said "measured" about something. This one is about how to measure, and about what you can truthfully conclude. For a Senior or Staff interview it is the most reusable tutorial in the track: whatever the question (a slow service, a regression, a benchmark someone quotes, a chip being proposed), the answer is a model plus a measurement plus an honest statement of what the measurement cannot see.

**Where the numbers come from.** The virtual machine of tutorials 14 to 27 (two vCPUs of an Intel Xeon under KVM, Linux 6.18) is again the test bench, and it is a poor one for half of this subject. **It has no performance counters** (no `perf` tool, no `arch_perfmon` flag, no `cpu` entry under `/sys/bus/event_source`), **no frequency or thermal interface** (no `cpufreq`, no `thermal`, no `powercap` directory; the idle driver is `none`), and the platform-energy counter it advertises (`power/energy-psys`) did not advance at all over three seconds of busy loop. So every CPI-stack, top-down and power figure below comes from papers, kernel documentation or arithmetic, and says so. What *can* be measured is everything that needs only a clock: the real core frequency (via dependent-instruction chains), timer cost and resolution, warm-up, run-to-run noise, the latency distribution of a loop that does nothing, memory bandwidth and peak floating-point rate (the two numbers of a roofline), and matrix-multiply efficiency. Those were measured by short C and Python programs, shown where they matter, pinned to one CPU unless stated. Published sources, linked where used: the Wikipedia articles on [Moore's law](https://en.wikipedia.org/wiki/Moore%27s_law) and [Dennard scaling](https://en.wikipedia.org/wiki/Dennard_scaling); Esmaeilzadeh et al., ["Dark Silicon and the End of Multicore Scaling"](https://www.microsoft.com/en-us/research/?p=1041987) (ISCA 2011); Dean and Barroso, ["The Tail at Scale"](https://cacm.acm.org/research/the-tail-at-scale/) (CACM 2013); the [SPEC CPU2017 overview](https://www.spec.org/cpu2017/docs/overview.html); the MLPerf Inference paper, ["MLPerf Inference Benchmark"](https://arxiv.org/pdf/1911.02549); the [wrk2 README](https://github.com/giltene/wrk2) on coordinated omission; Brendan Gregg's pages on [flame graphs](https://www.brendangregg.com/flamegraphs.html) and [CPU flame graphs](https://www.brendangregg.com/FlameGraphs/cpuflamegraphs.html); Anton Ertl's note on [zero-cycle constant adds](https://www.complang.tuwien.ac.at/anton/additions); and the Linux documentation for [cpuidle](https://docs.kernel.org/admin-guide/pm/cpuidle.html), [cpufreq](https://docs.kernel.org/admin-guide/pm/cpufreq.html), [intel_pstate](https://docs.kernel.org/admin-guide/pm/intel_pstate.html) and [powercap](https://docs.kernel.org/power/powercap/powercap.html).

> **The one-sentence model.** Run time is instructions times cycles per instruction divided by the clock, bounded by two walls (memory and power); and every layer of a measurement lies a little (the clock is not the label on the box, the timer is not free, the average is not the experience, the benchmark is not your workload), so a number is only as good as your account of what it excludes.

---

## 1. Why performance stopped being free

### 1.1 Moore's law, and what it never promised

In 1965 Gordon Moore observed that the number of components on an integrated circuit had been doubling about every year and expected that to continue for at least a decade. In 1975 he revised the period to about two years. That is the whole law: **a statement about how many transistors a manufacturer can economically put on a chip.** It says nothing about clock speed, instructions per second or watts. For about thirty years the transistors happened to arrive together with speed, because of the next subsection.

The pace has slowed: the Wikipedia summary cites industry-wide advance below Moore's pace since about 2010, an Intel chief executive saying in 2015 that the cadence was closer to two and a half years, and another saying in 2023 that it was nearer three. Whether to call the law dead is a matter of opinion and marketing (executives at Nvidia and Intel have taken opposite positions in public). The engineering fact that matters for you: **transistors still get cheaper slowly, they no longer get faster and cooler for free, and a new process node costs more each time** (the cost of a fabrication plant rising exponentially is sometimes called Rock's law).

### 1.2 Dennard scaling: the free lunch

In 1974 Robert Dennard and colleagues at IBM described how to shrink a MOS transistor so that it got better in every way at once. Scale every linear dimension by about 0.7 (a 30 % shrink) **and the supply voltage by the same factor**, and:

| Quantity | Factor per generation | Why |
|---|---|---|
| Transistor area | 0.7 × 0.7 = 0.49 (about half) | two dimensions |
| Transistors per chip of the same area | about 2 | the previous line, inverted |
| Capacitance *C* | 0.7 | proportional to size |
| Supply voltage *V* | 0.7 | keeps the electric field constant |
| Delay | 0.7 | smaller, lower-capacitance transistors switch faster |
| Frequency *f* | 1 ÷ 0.7 ≈ 1.4 | the previous line, inverted |
| Power per transistor, *C V² f* | 0.7 × 0.49 × 1.4 ≈ 0.48 (about half) | from the dynamic power equation of [tutorial 18](18_multicore_and_multithreading.md) section 1.1 |
| **Power per unit area** | 2 × 0.48 ≈ **1.0** | twice the transistors, each at half the power |

Read the last row twice. Each generation gave twice the transistors, each 40 % faster, in the *same power budget per square millimetre*. That is why a 1995 programmer could write slow code and wait for the next processor: clock speed grew from tens of megahertz to a few gigahertz on this schedule.

It stopped around 2005 to 2007. The rule needs the voltage to shrink with the dimensions, and it could not: below a certain voltage the transistor no longer switches off cleanly. **Leakage current** (the current that flows through a transistor that is nominally off) and the threshold voltage do not shrink with size. With *V* stuck, shrinking still doubles the transistors but their power no longer halves, so power density rises. The result was the **power wall**: Wikipedia records Intel cancelling two planned high-frequency designs in 2004, and clock rates have stalled at roughly 4 to 6 GHz since about 2005, in parts of around 100 W. This machine is a good illustration. Its label says 2.1 GHz and its core measures near 3 GHz (section 2.2); a decade of process shrinks has not changed that order of magnitude.

### 1.3 What replaced it, and the dark silicon forecast

The industry spent its transistors on other things, which is the plot of this whole track: more cores at moderate clocks ([tutorial 18](18_multicore_and_multithreading.md)), bigger and smarter caches (13 to 16), wider vector units (21), GPUs and accelerators (22 to 24). The sobering forecast is by Esmaeilzadeh and co-authors (ISCA 2011). They modelled multicore scaling under realistic power limits and concluded that, at 22 nm, **21 % of a fixed-size chip must be powered off** (the dark part), growing to **more than 50 % at 8 nm**; that only about **7.9 times** average speedup was achievable through 2024 for common parallel workloads; and that this left a nearly 24-fold gap from the historical target of doubling performance every generation. These are model projections from 2011, not measurements, and the real industry took the specialisation route they pointed to. The lesson you can use: **a chip is power-limited, so "more of the same" stopped being the winning move and doing the same work with fewer joules (specialisation, locality, lower precision) became the main lever.**

### 1.4 Die size, yield and cost

Silicon is bought by the wafer and sold by the die. Three formulas turn that into numbers.

```text
dies per wafer  ≈  π (d/2)² / A   −   π d / √(2 A)        d = wafer diameter, A = die area
yield (Poisson) =  e^(−D × A)                               D = defect density per area
good dies       =  dies per wafer × yield
```

The first term is the wafer's area divided by the die's; the second subtracts the partial dies around the edge. Defects land at random, so the chance that a die has none falls exponentially with its area. **Worked example**, with an *assumed* defect density of 0.1 per cm² (to illustrate; real figures are trade secrets and change as a process matures), a 300 mm wafer, and the Poisson model (a negative-binomial model with clustering gives yields within a few percent here):

| Die area | Dies per wafer | Yield | Good dies per wafer |
|---|---|---|---|
| 100 mm² | 640 | 90.5 % | 579 |
| 150 mm² | 416 | 86.1 % | 358 |
| 600 mm² | 90 | 54.9 % | 49 |

A die six times bigger yields **about twelve times fewer good dies** (579 against 49), so each costs about twelve times as much, not six. Now the economic argument for **chiplets**. Build the 600 mm² product from four 150 mm² chiplets instead: 358 good chiplets make 89 products per wafer against 49 monolithic, **1.8 times more**, provided each chiplet can be tested before packaging (so a bad one is discarded alone) and the links between chiplets cost acceptable area, power and latency. The model ignores packaging, testing, and the cost of the die-to-die interface, which is why the real answer is more modest and depends on volume.

What to take away for an engineer who does not design chips: **big dies are disproportionately expensive, which is why GPUs with huge dies are expensive and why vendors sell the same silicon in several product tiers** (parts with a defective core or cache slice are sold as a lower tier), why core counts rise by chiplets, and why "just add more cache" has a price.

---

## 2. The iron law, measured

### 2.1 The law and its three levers

[Tutorial 01](01_how_a_computer_works.md) gave the law:

```text
time  =  instructions  ×  cycles per instruction (CPI)  ×  seconds per cycle
      =  instructions  ×  CPI  ÷  frequency                  IPC = 1 / CPI
```

Each factor belongs to a different engineer: the **instruction count** to the algorithm, compiler and instruction set; **CPI** to the microarchitecture (pipeline, caches, predictors: [tutorials 9 to 16](09_pipelining_and_hazards.md)); the **frequency** to circuit design, process and the power budget. Any proposal to make something faster must say which factor it moves and what it does to the other two. A vector instruction lowers the count and raises CPI. A deeper pipeline raises the frequency and raises CPI. A bigger cache lowers CPI and may lower the frequency.

**Worked example.** Two machines run the same program. X is a 3.5 GHz core with CPI 1.2 on a program of 1.0 billion instructions. Y is a 2.8 GHz core with CPI 0.7, but its instruction set needs 15 % more instructions (1.15 billion) for the same work. Times: X is `1.0e9 × 1.2 ÷ 3.5e9` = **0.343 s**; Y is `1.15e9 × 0.7 ÷ 2.8e9` = **0.2875 s**. Y is 19 % faster despite the lower clock and the longer program. That is the content of "GHz is not speed", with numbers.

### 2.2 Which clock? The one on the box is not the one the core runs at

To turn a time into cycles you need the frequency the core actually ran at. This machine shows how wrong the obvious sources can be:

- `/proc/cpuinfo` says `cpu MHz : 2100.000`, `lscpu` says "@ 2.10GHz", and the kernel log says `tsc: Detected 2100.000 MHz processor`. The label is the part's base frequency, and the kernel's number is the rate of the **time-stamp counter (TSC)**, which on modern x86 ticks at a fixed rate whatever the core is doing (the `constant_tsc` and `nonstop_tsc` flags say so).
- A core **boosts** above base when the power and thermal budget allows ([tutorial 18](18_multicore_and_multithreading.md) section 1.3) and drops back under load or heat. A guest cannot see it: there is no `cpufreq` directory here.

You can find the real clock with nothing but a stopwatch, using an instruction whose latency in cycles you know. A chain of `add`s where each needs the previous result cannot go faster than one per cycle, so the time per add *is* one cycle:

```c
// clock.c: gcc -O2 clock.c -o clock && taskset -c 0 ./clock
#include <stdio.h>
#include <stdint.h>
#include <time.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }
#define A8 "add %1,%0\n add %1,%0\n add %1,%0\n add %1,%0\n add %1,%0\n add %1,%0\n add %1,%0\n add %1,%0\n"
int main(void) {
    uint64_t a = 0, b = 1; const long N = 400000000;
    double t0 = now();
    for (long i = 0; i < N / 8; i++) __asm__ volatile(A8 : "+r"(a) : "r"(b));   // each add waits for the one before
    double ns = now() - t0;
    printf("%.3f ns per dependent add = %.2f GHz\n", ns / N, N / ns);
    return 0;
}
```

*Measured* here, about twenty runs on both CPUs, with the other CPU idle and busy: **0.31 to 0.36 ns per add, which is 2.8 to 3.3 GHz**, never 2.1. A second, independent check: a chain of dependent `imul`s (latency 3 cycles) took 0.97 to 0.99 ns each, which is 2.9 to 3.0 cycles of that same clock. And a third, comparing against the TSC (`rdtsc` before and after the chain): **0.65 to 0.76 TSC ticks per core cycle**, so the core ran about 1.4 times faster than the TSC. (Read as cycles of a 2.1 GHz clock, one dependent add would take 0.7 cycles, which is impossible.) The core's clock also moved by a few percent from run to run, which is turbo at work and one reason benchmarks drift (section 4).

Two consequences you must carry forward.

- **A cycle count obtained as `time × nominal GHz` is a count of TSC ticks, not core cycles**, and can be off by 40 %. [Tutorials 26](26_interrupts_exceptions_and_dma.md) and [27](27_hardware_virtualization.md) converted this VM's nanoseconds to cycles at 2.1 GHz; read those as TSC ticks. In core cycles at 3.0 GHz, a 7 µs `cpuid` exit is about **21,000 cycles** (14,700 ticks), and a 115 ns system call about **345 cycles** (240 ticks). Ratios are unaffected (an exit is still about 60 system calls), which is why those tutorials concentrated on ratios.
- **`perf stat` counts core cycles** (`cycles`) and also reference cycles at the TSC rate (`ref-cycles`); their ratio is the average clock. On bare metal, `turbostat` reports the busy frequency directly. In your interviews, say "at what frequency?" before accepting any cycle figure.

### 2.3 CPI by experiment

With the clock known (3.0 GHz, which I use for the cycle column; treat it as ±8 %), the same chains measure the core. *Measured*, nanoseconds per instruction, pinned to CPU 0:

| Test | ns per instruction | Cycles at 3.0 GHz | What it shows |
|---|---|---|---|
| dependent `add reg,reg` | 0.32 to 0.34 | 1.0 | latency 1, so CPI 1 on a chain |
| dependent `imul` | 0.97 to 0.99 | 2.9 to 3.0 | latency 3 |
| dependent 512-bit FMA | 1.26 to 1.35 | about 4 | latency 4 |
| 2 independent `add` chains | 0.16 to 0.18 | 0.5 | two per cycle |
| 4 independent chains | 0.097 to 0.103 | 0.3 | about 3.3 per cycle |
| 8 independent chains | 0.077 to 0.083 | 0.24 | about **4.1 per cycle** |
| dependent `add reg,imm` | 0.079 to 0.107 | 0.24 to 0.32 | **faster than its own latency** (below) |

The same instruction, `add`, has a CPI of 1.0 when each depends on the last and 0.24 when eight chains overlap; add the loop's own counter and branch and the core sustained an **IPC of about 5**. *CPI is a property of the code's dependences as much as of the core.* This is [tutorial 11](11_superscalar_and_out_of_order_execution.md)'s lesson measured: the hardware has width, and only independent work can fill it.

The last row is a trap, discussed in section 4: a chain of `add $1` should be latency-bound at one per cycle, yet it ran at three to four per cycle. This core (a Golden Cove family design, as in the Emerald Rapids Xeons of [tutorial 14](14_cache_optimisation_and_the_real_hierarchy.md)) appears to **fold constant additions into register renaming** and execute them in zero cycles, as Anton Ertl measured on a Raptor Cove or Golden Cove core: it works for `add reg, imm` and `lea` but not for `add reg, reg`, and it breaks when the running sum leaves a small range. The moral for anyone measuring latency: use a dependent chain through *registers*, and check the result against the latency you expect.

### 2.4 Why MIPS and FLOPS mislead

- **MIPS** (millions of instructions per second) divides by the instruction count, which depends on the instruction set and compiler. A machine that needs three instructions where another needs one can have a higher MIPS and a longer run time. The iron law's instruction count is a *per program, per ISA* quantity; compare run time, not MIPS.
- **FLOPS** (floating-point operations per second) has more traps. **Which precision?** (FP64, FP32, FP16 and tensor-core numbers differ by orders of magnitude: [tutorial 23](23_gpu_memory_tensor_cores_and_scaling.md).) **How is an FMA counted?** (Two operations: one multiply, one add.) **Peak or sustained?** This VM's two cores have a theoretical FP64 peak of `2 cores × 2 FMA units × 8 lanes × 2 flop × 3.0 GHz` = **192 Gflop/s**; I measured 176 to 182 with a purpose-built loop, **117 to 169** with a tuned library matrix multiply on two threads, and **1 to 2** with the three-line triple loop a student writes (section 7.3). All of those are "Gflop/s on this machine".
- **Peak rates** are products of datasheet numbers and say what the machine cannot exceed, not what your code will do.

A single speed number is useful only with its denominator: for which program, with what compiler flags, at what clock, for how long, with what memory behaviour.

---

## 3. Benchmarks, and how to summarise them

### 3.1 What a benchmark is for

A benchmark stands in for a workload you do not have in the room. Ordered by realism: a **datasheet peak** (no workload at all), a **kernel** (a small loop that stresses one resource: STREAM for memory bandwidth, a matrix multiply for arithmetic), a **suite of real programs** (SPEC CPU, MLPerf, the TPC database benchmarks), and **your own workload** under production-like load. Each step toward your own workload is more relevant and less comparable. The rule is to use the least realistic benchmark that still exercises the thing you are deciding about, and never to extrapolate from a kernel that stresses something else.

### 3.2 SPEC CPU2017 as the worked example of method

The SPEC CPU2017 overview (linked above) shows how a careful suite is built, and its terms are what interviewers expect you to know.

- **Four suites**: SPECspeed 2017 Integer and Floating Point (10 benchmarks each) and SPECrate 2017 Integer (10) and Floating Point (13): 43 programs in C, C++ and Fortran.
- **Speed against rate.** *Speed* runs one copy of each benchmark and asks how fast it finishes (the tester may use OpenMP threads). *Rate* runs *N* copies at once and asks how much work gets done per unit time (OpenMP disabled; the tester picks *N*). Speed measures latency; rate measures throughput.
- **Ratio** for each benchmark: reference-machine time divided by the system's time (for rate, multiplied by the number of copies). The reference machine is a historical Sun Fire V490 with 2100 MHz UltraSPARC-IV+ chips, chosen only to make ratios dimensionless; SPEC states that the choice of reference does not change the relative performance of two systems.
- **Repeat runs**: three runs and the tools take the median, or two runs and the tools take the slower.
- **Overall score**: the **geometric mean** of the ratios.
- **Base and peak.** *Base* requires the same compiler flags, in the same order, for all modules of one language in the suite, and every published result must include it. *Peak* is optional and allows per-benchmark flags and feedback-directed optimisation. A legal base result is legal under the peak rules, not the reverse.

### 3.3 Why the geometric mean, and when not to use it

Suppose two programs and two machines, times in seconds:

| | Machine A | Machine B |
|---|---|---|
| Program 1 | 10 | 20 |
| Program 2 | 1,000 | 500 |
| **Total time** | 1,010 | 520 |

Normalise to A (ratio = B's time ÷ A's time): 2.0 and 0.5. The *arithmetic* mean of the ratios is 1.25, which says "B is 25 % slower". Normalise to B instead (A's time ÷ B's time): 0.5 and 2.0, arithmetic mean again 1.25, now "A is 25 % slower". **The arithmetic mean of ratios contradicts itself depending on which machine you call the reference.** The geometric mean (the *n*th root of the product) is 1.0 both ways: A and B are equal in this sense. It has the property that **the ratio of the geometric means equals the geometric mean of the ratios**, so the ranking cannot depend on the reference. That consistency is why SPEC uses it.

The geometric mean is *not* "total run time": here B finishes the pair in 520 s against A's 1,010 s, 1.94 times faster, and the geometric mean says tie. The right summary depends on the question:

| You want | Use | Why |
|---|---|---|
| a consistent ranking across a suite of different programs | geometric mean of ratios | independent of the reference machine |
| the time to run a known fixed workload (a nightly job list) | total (or arithmetic mean) of **times** | it is the time you pay |
| average of **rates** where each test does equal work (MIPS, requests per second over equal batches) | **harmonic** mean of rates | equal work means averaging the times |
| a weighted mix (80 % program 1, 20 % program 2) | weighted arithmetic mean of times, with the weights from your workload | the weights are the point |

Harmonic mean example: a machine does one task at 100 per second and another at 10 per second, equal work in each. The arithmetic mean of the rates, 55, is wrong: doing one unit of each takes `1/100 + 1/10` = 0.11 s, an average rate of `2 ÷ 0.11` = **18.2** per second, which is the harmonic mean `2 ÷ (1/100 + 1/10)`.

### 3.4 MLPerf Inference: scenarios that force honest latency

The MLPerf Inference paper (linked above; later versions of the suite changed some benchmarks and bounds) defines four **scenarios**, because one number cannot describe an inference service:

| Scenario | How queries arrive | What is reported |
|---|---|---|
| Single-stream | one at a time; the next is sent after the previous finishes | 90th-percentile latency |
| Multistream | a query of several samples at a fixed interval; at most 1 % may miss an interval | the number of streams the system sustains |
| **Server** | single-sample queries at random times (a Poisson process) | queries per second, **subject to a latency bound on a high percentile** (99th for vision tasks and 97th for translation in the paper, with bounds from 10 to 250 ms depending on the task) |
| Offline | all samples available at once | throughput, no latency bound |

The server scenario is the one that matches a production service, and its design carries two lessons for the rest of this tutorial: arrivals are *random*, not evenly spaced (so queues form: section 6.5), and the score is a throughput **under a tail-latency constraint** (section 6.2), not the peak.

### 3.5 Reading a vendor's claim

A short checklist that catches most misleading numbers: **against what baseline** (which previous part, which software version)? **Which metric** (speed or rate; base or peak; mean or percentile)? **What workload** (is it yours)? **What was held constant** (clock, cores, memory, compiler)? **At what power** (a result at 400 W is not a result at 100 W)? **Reproducible** (can you run it)? A claim that omits two of these is marketing.

---

## 4. Microbenchmarking: how to fool yourself

A microbenchmark times a few lines of code in a loop. It is the right tool for "how many cycles does this instruction take" and the wrong tool for most other questions, and the number it returns is wrong more often than right unless you check it. These are the traps, each measured on this machine.

### 4.1 The compiler deletes your work

```c
long s = 0; for (long i = 0; i < 1000000000L; i++) s += i * 3;   // time this loop
```

| Variant | Time for 10⁹ iterations |
|---|---|
| `-O2`, result never used | **0.001 ms** |
| `-O2`, result printed afterwards | **0.0 ms** (the compiler computed the closed form and printed a constant) |
| `-O2`, accumulator declared `volatile` | 921 ms (0.92 ns per iteration) |
| `-O0`, result never used | 2,317 ms (2.3 ns per iteration) |

The first two rows are "infinitely fast" code: a billion iterations in a microsecond is 10¹⁵ per second, which no machine does. Treat any result that beats the hardware as a bug in the benchmark. The `volatile` row shows the opposite error: the loop now measures a store followed by a dependent load of the same address, **about 3 cycles per iteration**, and says nothing about arithmetic. Making the compiler keep a result without changing the code's nature takes an empty `asm` with the value as an input (`keep()` in the harness below). Always read the generated assembly of a hot loop once (`gcc -O2 -S`, or [Compiler Explorer](https://godbolt.org)).

### 4.2 The timer is not free, and not instant

*Measured:* `clock_gettime` costs **30 to 33 ns** per call here (the smallest gap between two back-to-back calls was 30 ns). Timing one floating-point multiply with a call before and after gave a median of **31 ns** (min 29, max 37): that is the timer, not the multiply. Put the same multiply in a loop of 10⁸ and you get 3.2 ns, which is again not the multiply (it is the `volatile` round trip of the previous paragraph; a bare dependent multiply is 4 cycles, about 1.3 ns). Two rules follow. **Time many iterations and divide**, so that the timer's cost is a negligible fraction. And **the shortest interval you can measure honestly is a few hundred times the timer's cost**.

`rdtsc` reads the TSC without a system call: back-to-back reads were 28 ticks apart, **13 ns**. Beware that it is not serialising: the processor may execute it earlier or later than neighbouring instructions, so bracket short intervals with `lfence` or use `rdtscp`. And it counts TSC ticks, not core cycles (section 2.2).

### 4.3 Warm-up: the first run is a different experiment

The first touch of freshly allocated memory costs a page fault per 4 KiB page. *Measured:* writing 256 MiB for the first time took **140 ms (2.1 µs per page)**, while a later read pass over the same memory took 50 ms (0.75 ns per `int`). A benchmark whose first call allocates and fills is three times slower than its second. Other warm-up effects: instruction cache and branch predictors start cold, dynamic linking resolves symbols on first call, the TLB and caches fill, the core's clock ramps up, and a JIT-compiled language runs interpreted code until the compiler has seen enough calls. **Discard the first few repetitions, and say so.**

### 4.4 Noise: run it many times and look at the distribution

*Measured:* the same CPU-bound loop (about 96 ms) run 60 times on a pinned CPU:

| First run | Min | Median | Mean | 90th percentile | Max |
|---|---|---|---|---|---|
| 99.2 ms | 84.1 ms | 95.9 ms | 95.9 ms | 103.3 ms | 105.3 ms |

Nothing about the code changed between runs. The maximum is 25 % above the minimum, from three sources visible in this tutorial: the core's clock drifting between roughly 2.8 and 3.3 GHz (section 2.2), the hypervisor taking the CPU away for tens of microseconds about three thousand times a second (section 6.3), and whatever else shares the host. Which number to report depends on the question: the **minimum** estimates what the code *can* do on this machine (it removes interference, which is only additive), the **median** what it *typically* does, and a high percentile what a user will *sometimes* see. A **mean of a handful of runs** is the worst choice, because one outlier moves it. State the number of runs and the spread.

### 4.5 The impossible number

My own first attempt to measure this core's peak floating-point rate is the best example. The loop kept twelve independent chains of fused multiply-adds in twelve vector registers, and printed **149 Gflop/s** per thread. The arithmetic bound is `2 FMA units × 8 lanes × 2 flop × 3.0 GHz` = **96 Gflop/s**. A result above the hardware limit cannot be right, and the cause was that all twelve registers were initialised to the same value and updated by the same operations, so the compiler noticed they were identical and **merged them into one chain** (the measured time per FMA was exactly the single-chain latency divided by twelve). Starting each chain from a different value, taken from a variable the compiler cannot see through, gave **86 to 93 Gflop/s**, within 10 % of the bound. The check that saved the result was not skill but a **sanity bound**: every microbenchmark should be compared with a number the hardware makes impossible to beat.

The same thing happened to the first version of the bandwidth program in Problem D. It ran the triad `a[i] = b[i] + s * c[i]` over three 128 MiB arrays in a loop of repeated passes (with `s` changed each pass so that no pass was redundant) and reported **61 GB/s**, nearly five times what a single core of this machine can pull from DRAM. At `-O2` it reported 11.6 GB/s, so the optimiser was responsible, and `-fopt-info-loop-optimized` named the transformation: *loops interchanged in loop nest*. GCC at `-O3` swapped the repetition loop with the array loop, which is consistent with each element being loaded once and used for all the passes: the memory traffic per pass shrinks, and the program is then limited by something other than DRAM. Putting an empty compiler barrier between the passes (`__asm__ volatile("" ::: "memory")`) restored 12.4 to 13.0 GB/s. The sanity bound here was the DRAM bandwidth already measured by another program.

### 4.6 The hardware shortcuts your test

The dependent `add $1` chain of section 2.3 ran faster than the instruction's one-cycle latency because the renamer folds constant additions. A "latency test" built from adds-of-constants measures the renamer. The cure is to know what you expect and to vary the test: a chain through registers showed the true latency of 1.

### 4.7 The data and the machine state are part of the benchmark

The same summing loop ran at **0.75 ns per element** over a 256 MiB array (DRAM-bound) and **0.18 ns per element** over 4 MiB (cache-resident, and vectorised): a factor of four for the same code, because the working set changed. Earlier tutorials gave other cases: sorted against random data for a branchy loop ([tutorial 10](10_branch_prediction.md)), array size and stride ([tutorials 13 and 14](13_cache_basics.md)), pages and TLB reach ([tutorial 15](15_virtual_memory_and_the_tlb.md)), NUMA placement ([tutorial 16](16_dram_memory_controllers_and_numa.md)), contended against uncontended locks ([tutorial 19](19_cache_coherence.md)). **A benchmark result is a function of the input, the size, the alignment and the placement as well as the code**; state them.

### 4.8 A harness that avoids most of this

```c
// bench.c: gcc -O2 bench.c -o bench && ./bench
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sched.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }
static inline void keep(long v) { __asm__ volatile("" : : "r"(v) : "memory"); }   // the compiler must assume v is used
static int cmp(const void *a, const void *b) { double x = *(double *)a, y = *(double *)b; return (x > y) - (x < y); }
__attribute__((noinline)) static long work(const int *a, long n) { long s = 0; for (long i = 0; i < n; i++) s += a[i]; return s; }
int main(void) {
    cpu_set_t cs; CPU_ZERO(&cs); CPU_SET(0, &cs); sched_setaffinity(0, sizeof cs, &cs);   // 1. pin
    const long n = 1 << 20; int *a = malloc(n * sizeof *a);
    for (long i = 0; i < n; i++) a[i] = 1;                                                // touch the memory first
    enum { REPS = 31 }; double t[REPS];
    for (int r = -3; r < REPS; r++) {                                                      // 2. warm up: 3 runs discarded
        double t0 = now(); long s = work(a, n); keep(s); double dt = now() - t0;           // 3. consume the result
        if (r >= 0) t[r] = dt;
    }
    qsort(t, REPS, sizeof *t, cmp);                                                        // 4. report a distribution
    printf("min %.1f us, median %.1f us, p90 %.1f us, max %.1f us (%.2f ns per element at the median)\n",
           t[0] / 1e3, t[REPS / 2] / 1e3, t[(int)(REPS * 0.9)] / 1e3, t[REPS - 1] / 1e3, t[REPS / 2] / n);
    return 0;
}
```

*Measured* (two runs): median 185 and 189 µs for 4 MiB, 0.18 ns per element, with the minimum 4 to 9 % below the median and the maximum 57 to 87 % above it. The checklist it encodes, with the remaining items it cannot:

1. **Say what you are measuring** and write down the number you expect first (a latency from a manual, a bandwidth limit, a roofline). A result far from it is a finding or a bug.
2. **Pin** the thread, and record the machine (CPU model, clock behaviour, VM or bare metal, kernel).
3. **Warm up**, then **repeat**; report min, median, a high percentile and the number of runs.
4. **Consume the result** and look at the assembly once.
5. **Pick sizes and inputs on purpose** (cache-resident, DRAM-resident; sorted, random) and report them.
6. **Compare with a sanity bound** (peak rate, bandwidth, the roofline of section 7).
7. **Change one thing at a time**, and re-run the baseline in the same session, because drift (frequency, other tenants) is real.
8. **Beware sustained against burst**: a 10 ms test runs at turbo; a 10 minute test runs at the power and thermal limit (section 9.5).

---

## 5. Amdahl, scaling and the limits of optimisation

### 5.1 Amdahl's law as a tool for choosing what to optimise

[Tutorial 18](18_multicore_and_multithreading.md) section 6 used Amdahl's law for adding cores. It is just as useful for deciding where to spend your time. If a fraction *f* of the run time is sped up by a factor *s* and the rest is unchanged,

```text
overall speedup  =  1 / ( (1 − f)  +  f / s )          limit as s → ∞:  1 / (1 − f)
```

**Worked example.** A request handler spends 10 % parsing, 5 % authenticating, 55 % in the database, 25 % rendering and 5 % logging. Overall speedup from each candidate change:

| Change | Overall speedup |
|---|---|
| database made 2× faster | 1 / (0.45 + 0.275) = **1.38×** |
| database made 10× faster | **1.98×** |
| database made infinitely fast | **2.22×** (the ceiling: 1 / 0.45) |
| rendering made 10× faster | 1 / (0.75 + 0.025) = **1.29×** |
| rendering made infinitely fast | 1.33× |
| parsing made infinitely fast | 1.11× |

The table is the whole method: the benefit of a change is bounded by the fraction of time it touches, so **profile first, and spend effort in proportion to the time share, not to how interesting the code is**. The last row is the usual surprise: perfect parsing buys 11 %. It also shows diminishing returns: the database from 2× to 10× faster gains 0.6, and from 10× to infinity gains 0.24. After every successful optimisation the profile changes (the next bottleneck takes over: [tutorial 12](12_inside_a_modern_core.md) section 9), so re-measure.

### 5.2 Parallel scaling, in a paragraph

For parallel programs the same law has *s* = the number of cores, and [tutorial 18](18_multicore_and_multithreading.md) section 6 added **Gustafson's law** (a problem that grows with the machine scales much better: strong against weak scaling) and the **Karp-Flatt metric** (compute the serial fraction from a measured speedup: if it is constant it is Amdahl, if it grows with the number of cores something costs more as you add them). Use those; do not repeat them here.

### 5.3 When adding threads makes it slower: the Universal Scalability Law

Amdahl's curve rises and flattens. Real throughput often rises, flattens and then **falls**, because threads also pay for coordinating with each other. Neil Gunther's Universal Scalability Law models relative capacity with two coefficients:

```text
C(N)  =  N / ( 1 + α (N − 1) + β N (N − 1) )
         α = contention (the serial fraction: waiting for a shared resource)
         β = coherency (crosstalk: the cost of keeping each thread's view consistent)
```

With β = 0 it is Amdahl. The quadratic β term models what [tutorial 19](19_cache_coherence.md) showed: cache-line ping-pong, lock hand-offs and invalidation traffic grow with the *pairs* of threads. **Worked example**, with α = 0.05 and β = 0.0005 (assumed, as if fitted to measurements):

| Threads N | 1 | 2 | 4 | 8 | 16 | 32 | 44 | 64 | 96 |
|---|---|---|---|---|---|---|---|---|---|
| Capacity C(N) | 1.00 | 1.90 | 3.46 | 5.81 | 8.56 | 10.51 | **10.74** | 10.38 | 9.31 |

The peak is at `N* = √((1 − α) / β)` = **44** threads, and adding more *reduces* throughput: 96 threads deliver less than 32. To use the model you measure throughput at several thread counts, fit α and β, and read off where the knee is. The practical message: if a scaling curve turns down, you are not in Amdahl's world any more; look for a lock, a shared counter, false sharing, or a memory-bandwidth limit, and reduce sharing rather than adding hardware.

### 5.4 Offload: the transfer is part of the serial fraction

Sending work to an accelerator adds a term. If a fraction *f* is sped up by *s* but moving data costs a further fraction *o* of the original time, the speedup is `1 / ((1 − f) + f / s + o)`. **Example**: a program whose kernel is 70 % of the run is moved to a GPU that does it 20 times faster. With free transfers the result is 1 / (0.30 + 0.035) = **2.99×**. If the copies over the bus cost 6 % of the original time, the result is 1 / (0.30 + 0.035 + 0.06) = **2.53×**; if they cost 40 %, only **1.36×**. This is why [tutorial 23](23_gpu_memory_tensor_cores_and_scaling.md) section 9.6 and [tutorial 25](25_buses_pcie_and_io_mapping.md) section 3 spent so long on when a GPU loses and what a PCIe link costs.

---

## 6. Latency, throughput and the tail

### 6.1 Two different quantities

**Latency** is how long one operation takes from start to finish. **Throughput** is how many operations finish per unit time. They are linked but not the same: a pipeline raises throughput without lowering latency ([tutorial 9](09_pipelining_and_hazards.md)); batching raises throughput and raises latency (interrupt moderation and NAPI, [tutorial 26](26_interrupts_exceptions_and_dma.md) section 4; GPU batching, [tutorial 23](23_gpu_memory_tensor_cores_and_scaling.md)). A link's **bandwidth-delay product**, bandwidth times round-trip time, is the amount of data that must be in flight to keep it full: a 25 Gb/s link with a 100 µs round trip needs `25e9 / 8 × 100e-6` = **312,500 bytes** outstanding. A service level objective almost always has the form "throughput at least X *and* latency percentile at most Y", which is why the two must be reported together (the MLPerf server scenario of section 3.4).

### 6.2 Percentiles, and why the average is not the experience

The *p*th **percentile** is the value below which *p* percent of the samples fall: p50 is the median, p99 the value 1 % of requests exceed. A mean hides the slow requests that users remember; a service with a 5 ms mean can have a 500 ms p99. Four rules:

- **Report a high percentile with the median** (p50, p99, p99.9), and the number of samples. To estimate a percentile you need several samples beyond it: as a rule of thumb at least ten, so p99.9 needs on the order of 10,000 samples.
- **Never average percentiles** across servers or time windows; the average of two p99s is not a p99. Merge the underlying histograms (high-dynamic-range histograms are the standard tool) and take the percentile of the merge.
- **Latency is rarely normal**: it has a body and a long tail, so means and standard deviations mislead; look at a histogram or a cumulative curve.
- **The worst case is a distribution too**: the maximum grows with the number of samples, so quote it with its duration.

### 6.3 Measured: a loop that does nothing is interrupted three thousand times a second

To see a floor under any latency you can measure here, I ran the emptiest loop possible: read the TSC again and again, and record every gap between consecutive reads longer than 1 µs. On a quiet pinned CPU a normal iteration takes 16 ns, so any long gap is the program being *stopped*.

```c
uint64_t prev = __rdtsc(), end = prev + 5 * 2100000000ull;      // 5 seconds of TSC ticks
for (;;) { uint64_t t = __rdtsc(); if (t - prev > 2100) record(t - prev); prev = t; if (t > end) break; }
```

*Measured*, three runs of 10 s on CPU 0:

| Quantity | Result |
|---|---|
| Iterations | about 610 million (16.1 to 16.4 ns each) |
| Gaps longer than 1 µs | **2,763 to 3,271 per second** |
| Median gap | 4.6 to 10.5 µs |
| 90th percentile | 18 to 23 µs |
| 99th percentile | 43 to 106 µs |
| Maximum | 2.7 to 4.9 ms (up to 6.2 ms in a 5 s run) |
| **Share of the time lost in gaps** | **3.4 % to 4.2 %** |

Only a small part of that is the guest kernel. Over a 5 s run the local-timer interrupt counter in `/proc/interrupts` rose by 1,256 on CPU 0, which is **251 per second** (HZ = 250, [tutorial 26](26_interrupts_exceptions_and_dma.md) section 6.4); all the other interrupts on CPU 0 together numbered 57. So about nine in ten of the gaps are not interrupts the guest took. They are the host: the vCPU being descheduled for a moment, or exits the hypervisor handles ([tutorial 27](27_hardware_virtualization.md) sections 3 and 6.4). I cannot see which, and the guest's own steal counter, the one direct view of host interference, rose by only 10 ticks (0.1 s) during a 5 s run, against about 0.2 s lost in gaps: steal accounting reports part of this loss and not all. The spacing between large gaps was typically 0.1 to 0.8 ms and irregular.

What it means for latency: if interruptions arrive at rate λ (take 3,000 per second) and an operation takes *d*, the chance it overlaps one is about `1 − e^(−λd)` (treating arrivals as random, which these are not exactly):

| Operation duration *d* | Chance of being hit |
|---|---|
| 1 µs | 0.3 % |
| 10 µs | 3 % |
| 100 µs | 26 % |
| 1 ms | 95 % |

A request that takes 100 µs of CPU in this VM has about a one in four chance of being stretched by a typical 5 to 10 µs and a small chance of milliseconds, **before any queueing**. Everything that follows about tails starts from this: the tail has a floor set by the platform, and it is higher in a VM than on dedicated hardware. Pinning, isolated cores, dedicated hosts and removing background activity lower it; ignoring it makes a p99 target of 50 µs impossible.

### 6.4 Tail latency at scale

Dean and Barroso (*The Tail at Scale*, linked above) made the point that mattered for large services: **when a request fans out to many servers and waits for all of them, the slowest dominates.** Their example is a server that usually answers in 10 ms but takes one second at its 99th percentile, so one request in a hundred is slow. If a user request needs responses from 100 such servers, **63 % of user requests take more than one second.** The arithmetic is `1 − 0.99^N`:

| Servers contacted *N* | 1 | 10 | 100 | 1,000 |
|---|---|---|---|---|
| Fraction slow | 1.0 % | 9.6 % | **63.4 %** | 99.996 % |

So the tail of one component becomes the median of the system. (What a network adds to that tail, and the arithmetic of timeouts, hashing and incast, is in [tutorial 29](29_interconnection_networks_and_warehouse_scale_computing.md) section 8.4.) The paper's remedies, in its own groups: *within a request*, **hedged requests** (send the same request to a second replica if the first has not answered within, say, the 95th percentile, and use whichever returns first) and **tied requests** (send to two replicas that cancel each other when one starts); *across requests*, **micro-partitions** (many small partitions per machine so load can be rebalanced finely), **selective replication** of hot items, and **latency-induced probation** (stop sending to a slow server for a while); for search-style systems, **"good enough" results** and **canary requests** (try a new query on one or two servers first); and reducing the variability itself: **service classes and higher-level queueing, avoiding head-of-line blocking** behind long requests, and **managing background activities** (compaction, garbage collection, log rotation) so they do not synchronise. The first thing to do about a bad p99 is rarely a clever mitigation; it is to find the source of the variance, which for a CPU is the list in section 6.3, queueing (next section), and the memory-system and kernel effects of earlier tutorials.

### 6.5 Little's law and queues

**Little's law** (J. D. C. Little, 1961): in any stable system, the average number of items inside equals the arrival rate times the average time each spends inside,

```text
L  =  λ × W          (items in the system = arrival rate × time in the system)
```

It needs no assumption about distributions. Examples:

- A service receiving 200 requests a second with a mean latency of 50 ms has `200 × 0.05` = **10 requests in flight** on average. Sizing a thread pool or connection pool below that makes requests wait.
- **Memory-level parallelism** ([tutorial 14](14_cache_optimisation_and_the_real_hierarchy.md) section 3.2) is Little's law. This guest measured a dependent DRAM miss at 221 ns and found that 12 independent misses overlap, so random-access bandwidth is `12 × 64 B ÷ 221 ns` = **3.5 GB/s** per core. But the same core streams memory at **13.5 GB/s** (section 7.2). To sustain that with 221 ns per line it would need `13.5e9 ÷ 64 × 221e-9` = **47 lines in flight**, more than the 10 to 16 miss buffers it has. The resolution is that sequential accesses are cheaper than random ones (open rows, no TLB miss) and that hardware prefetchers fetch ahead into the L2. Little's law gave the check: "how many in flight does this bandwidth require?" is the first question for any bandwidth-latency puzzle.

**The hockey stick.** Little's law says nothing about *why* latency rises with load. The simplest queue, one server with random arrivals and random service times (M/M/1), gives a mean time in the system of

```text
W  =  S / (1 − ρ)           S = mean service time, ρ = utilisation = arrival rate × S
```

and, because the time in the system is then exponentially distributed, a 99th percentile of `W × ln(100)` = **4.6 × W**. For S = 1 ms:

| Utilisation ρ | 50 % | 80 % | 90 % | 95 % | 99 % |
|---|---|---|---|---|---|
| Mean time in system | 2 ms | 5 ms | 10 ms | 20 ms | 100 ms |
| 99th percentile | 9.2 ms | 23 ms | 46 ms | 92 ms | 460 ms |

(A simulation of 400,000 requests matched at 50 %, 80 % and 90 %: means 2.01, 4.99 and 10.1 ms, 99th percentiles 9.2, 23.1 and 46.4 ms; at 95 % it ran 9 % high on the mean and 20 % on the 99th percentile, which is the slow convergence of heavily loaded queues.) The shape is the lesson. Going from 50 % to 90 % busy makes the mean five times worse and the tail five times worse; from 90 % to 99 % another ten times. **A system at 90 % utilisation is not "almost full", it is already in the steep part of the curve.** This is why capacity planners keep latency-sensitive services below 60 to 70 % utilisation, and why any benchmark that reports latency without saying the offered load is meaningless.

### 6.6 Coordinated omission: the load generator that hides the tail

How you generate load changes what you measure. A **closed-loop** tester sends a request, waits for the reply, and sends the next. When the server stalls, the tester stalls too, so it sends *fewer* requests exactly while the system is slow and records one long latency instead of the many that real users, arriving on their own schedule, would have suffered. The wrk2 README calls this the coordinated omission effect. **Worked example.** A server normally answers in 1 ms and freezes for one second in the middle of a 10 s test; users arrive at 1,000 per second.

| Measured by | Samples | p50 | p99 | p99.9 | Mean |
|---|---|---|---|---|---|
| Closed loop (waits for each reply) | 9,001 | 1 ms | **1 ms** | **1 ms** | 1.1 ms |
| Open loop (latency charged from the intended send time) | 10,000 | 1 ms | **902 ms** | 992 ms | 51 ms |

A thousand users (10 % of all requests) waited between 1 ms and 1 second behind the freeze; the closed loop records one slow sample in 9,001 and reports a p99.9 of 1 ms. The fix, as in wrk2, is to generate requests at a constant rate on a fixed schedule and to measure each from the time it *should* have been sent (wrk2's `-R` option sets the rate). Whenever you see a latency histogram with a suspiciously clean tail, ask how the load was generated.

### 6.7 Throughput against latency is a trade, not a pair of goals

Almost every performance mechanism in this track moves along one curve: bigger batches (interrupt moderation, GPU kernels, disk writes), deeper queues, more parallelism and more utilisation raise throughput and raise latency; spare capacity, small batches and polling lower latency at a cost in throughput, power or CPU. Decide which the product needs, state it as a latency percentile at an offered load, and then tune to that: MLPerf's server scenario and a database's "99th percentile under 10 ms at 50,000 requests a second" are the same statement.

---

## 7. The roofline, measured on this CPU

### 7.1 The model in two lines

[Tutorial 12](12_inside_a_modern_core.md) section 9 introduced the idea and [tutorial 23](23_gpu_memory_tensor_cores_and_scaling.md) section 7 applied it to a GPU. A kernel is limited either by arithmetic or by memory, and which one depends on its **arithmetic intensity** (AI): the floating-point operations it performs per byte it moves to and from DRAM. The roofline model (Williams, Waterman and Patterson, 2009) says

```text
attainable flop/s  =  min( peak flop/s ,  memory bandwidth × AI )
ridge point        =  peak flop/s / bandwidth            (the AI where the two limits meet)
```

On a log-log plot of performance against AI this is a flat roof (peak) preceded by a sloped one (bandwidth). A kernel to the left of the ridge is memory-bound, to the right compute-bound, and the vertical gap between a measured point and the roof is the headroom you have not yet used. Two measurements build a CPU's roofline, and both can be made with the short programs of earlier tutorials.

### 7.2 Measuring the two roofs

*Measured* on this guest (4 KiB pages, 3 GHz core, pinned; the programs were OpenMP loops compiled with `-O3 -march=native`):

| Quantity | 1 thread | 2 threads |
|---|---|---|
| Peak FP64, AVX-512 fused multiply-add | 86 to 93 Gflop/s | 176 to 182 Gflop/s |
| Peak FP64, AVX2 fused multiply-add | 47 to 50 Gflop/s | 97 Gflop/s |
| Memory bandwidth, STREAM triad `a[i] = b[i] + 3 c[i]` (24 bytes counted per element), arrays of 128 MiB | **13.3 to 13.5 GB/s** | **25.5 to 26.3 GB/s** |
| Single-thread read-only sum, integer or reassociated FP | about 12 GB/s | |
| Triad on a 768 KiB working set (fits the 2 MiB L2) | 105 to 122 GB/s | |
| Triad on a 24 KiB working set (fits the 48 KiB L1) | 230 to 246 GB/s | |

The peaks match the arithmetic bound of section 4.5: `2 FMA units × 8 lanes × 2 flop × 3.0 GHz` = 96 for AVX-512 per thread, half that for AVX2, and two threads double both (so the two vCPUs do not share FMA units). The bandwidth rows say that one core can pull about 13 GB/s from DRAM and two cores 26 GB/s; the cache rows say the same loop runs **eight to nine times faster** when its data sit in L2 and about eighteen times when they sit in L1. That is the cache hierarchy of tutorials 13 to 16 expressed as a set of roofs: each level has its own sloped line. **The scalar ceiling** (no vectors, one FMA stream per unit) is lower: `2 units × 2 flop × 3.0 GHz` = **12 Gflop/s**, an eighth of the AVX-512 peak, so code the compiler did not vectorise ([tutorial 21](21_simd_and_vector_processing.md)) has a roof of 12, not 90.

*A trap in the bandwidth row.* My first read-only "bandwidth" test (a loop summing doubles) reported only **6.6 GB/s**. It was not measuring memory. Strict floating-point semantics forbid the compiler from reordering the additions, so each add waits for the previous one: 1.22 ns per element, about 3.7 cycles, the latency of a floating-point add. With integers, or with `-ffast-math` allowing reassociation and vector adds, the same loop reached **12 GB/s**. A bandwidth benchmark that is latency-bound is a latency benchmark.

With these numbers the roofline for one thread using AVX-512 is `min(90, 13.5 × AI)` Gflop/s and the **ridge point is 90 ÷ 13.5 = 6.7 flop/byte** (6.9 for two threads: 180 ÷ 26; 3.6 for AVX2 on one thread: 48 ÷ 13.5). Attainable performance by intensity (one thread, AVX-512):

| AI (flop/byte) | 0.0625 | 0.125 | 0.25 | 0.5 | 1 | 2 | 4 | 6.7 | 16 |
|---|---|---|---|---|---|---|---|---|---|
| Attainable Gflop/s | 0.84 | 1.7 | 3.4 | 6.8 | 13.5 | 27 | 54 | 90 | 90 |

### 7.3 Placing kernels

| Kernel | AI (flop per byte) | How AI is derived | Roofline bound, 1 thread | Measured |
|---|---|---|---|---|
| STREAM triad | 0.083 | 2 flop per 24 B | 1.1 Gflop/s | 1.1 (by construction) |
| Dot product of two double arrays | 0.125 | 2 flop per 16 B | about 1.5 (at about 12 GB/s) | not measured |
| Sparse matrix-vector multiply, CSR | at most 0.17 | 2 flop per 8 B value + 4 B index, vector cached | about 2 | not measured |
| Matrix multiply, naive `ijk`, 1024², FP64 | about 0.03 at the level where `B` misses | each 8 B use of `B` pulls a 64 B line: 2 flop per 64 B | (traffic is from L2 and L3, so the DRAM roof does not apply) | **1.1 (`-O2`), 2.0 (`-O3 -march=native`)** |
| Matrix multiply, loop order `ikj`, 1024² | much higher | `B` and `C` walked row by row, every line used in full | | **6.7 (`-O2`), 8.1 (`-O3 -march=native`)** |
| Matrix multiply, OpenBLAS, 2048² to 4096² | `n ÷ 12` = 171 to 341 | 2n³ flop over the 24 n² bytes of A, B and C read or written once | 90 (the compute roof) | **80 to 85 (one thread)** |

Read the table from the bottom. A library multiply reaches about **90 % of the measured peak** because blocking keeps tiles of the matrices in registers and caches, raising the intensity far beyond the ridge. (Single precision, with twice the lanes, reached 160 Gflop/s on one thread and 304 on two.) A student's triple loop reaches **1 to 2 %**, because each flop drags in a share of a cache line that is mostly unused; at n = 1024 the column of `B` is also an 8 KiB stride, which lands every element of a column on a few cache sets and adds the conflict misses of [tutorial 13](13_cache_basics.md). Swapping two loops (the same arithmetic, `B` now read along rows) is worth 4 to 6 times, and the library is another 10 times on top. **The same 2n³ flops run at rates from 1.1 to 85 Gflop/s**, and all of that factor is data movement. (Two-thread library results ranged from 117 to 169 Gflop/s, between 1.45 and 2.0 times one thread; the best runs reach the 2.0 the peaks suggest, and the spread between runs is itself a reminder of section 4.4.)

### 7.4 What the roofline tells you to do

- **Left of the ridge (memory-bound).** Raise the intensity: block or tile for the cache, fuse loops so intermediates stay in registers, use a smaller data type (FP32 or INT8 halves or quarters the bytes), compress, or reorder the algorithm. More vector width will not help; you are waiting for memory.
- **Right of the ridge (compute-bound).** Use the machine's full arithmetic width: vectorise, use FMA, keep several independent chains in flight (section 2.3), and use all the cores. Now the scalar and AVX2 ceilings matter.
- **Below both roofs by a lot.** Something else binds: latency (a dependent chain), the front end, branch misprediction, synchronisation. [Tutorial 12](12_inside_a_modern_core.md) section 9 gives the loop-level bounds for those.

### 7.5 Where the roofline lies

It counts floating-point operations against DRAM bytes, so it says nothing about **latency-bound** code: a pointer chase moves 64 B per 221 ns, **0.29 GB/s**, nearly fifty times below the memory roof, and no roofline will tell you why. It ignores instruction-bound code (integer, branchy, string processing). Its AI depends on the cache: the same loop has a different intensity at a different working-set size, and the true DRAM traffic needs counters to measure (this VM has none). And the roof is a ceiling, not a target: reaching 70 % of a roof on a real kernel is good work.

---

## 8. Where did the cycles go

### 8.1 A CPI stack

Break the CPI of a program into a base plus stall components, one row per cause. **Worked example** (illustrative rates in the style of [tutorial 13](13_cache_basics.md); a 3 GHz core):

| Component | Calculation | CPI contribution |
|---|---|---|
| Base (ideal execution) | | 0.40 |
| Branch mispredictions | 0.2 branches per instruction × 3 % mispredicted × 17-cycle penalty | 0.10 |
| L1 data misses that hit in L2 | 0.3 memory references per instruction × (5 % miss L1, 1 % of them go on to DRAM, so 4 % stop at L2) × 14 cycles | 0.17 |
| Misses served by DRAM | 0.3 × 1 % × 250 cycles | **0.75** |
| **Total CPI** | | **1.42** (IPC 0.70) |

The DRAM line is 53 % of the cycles. Now each candidate change has an arithmetic answer: halving the DRAM miss rate (blocking, better layout) takes the CPI to 1.045, a **1.36× speedup**; removing every branch misprediction takes it to 1.32, a mere **1.08×**. This is Amdahl's law for CPI: the stack tells you which row to attack, and the biggest row sets the ceiling for the others. The 250-cycle penalty is a round, optimistic number: the 221 ns pointer-chase hop of [tutorial 14](14_cache_optimisation_and_the_real_hierarchy.md) is about 660 cycles at 3 GHz (it includes a TLB miss), which would make the DRAM row larger still. And the stack is a *model*: an out-of-order core overlaps misses with each other and with useful work (tutorial 14 section 3), so the real stall is less than the sum, and an additive stack overstates it. That is why hardware reports slots rather than stall cycles.

### 8.2 Top-down, in a paragraph

[Tutorial 12](12_inside_a_modern_core.md) section 10 taught the method: count pipeline slots (width times cycles) and put each in one of four buckets (retiring, bad speculation, front-end bound, back-end bound), start at the biggest, descend one level, change one thing, measure again. Two reminders. **IPC is not a diagnosis**: a thread spinning on a lock retires happily. And **it needs hardware counters**, which this VM does not expose, so I can show you the method but not a profile from this machine. Without counters you still have: wall-clock and CPU time; an **IPC derived from a time and a clock** (instructions counted by construction or by `valgrind`, divided by `time × frequency`, with the frequency measured as in section 2.2); software events (page faults, context switches); and **time-based sampling**, which `perf record` falls back to (the `cpu-clock` event) when no hardware counter exists.

### 8.3 `perf` in practice

| Question | Command |
|---|---|
| Overall: time, cycles, instructions, IPC, branch and cache misses, and the average clock (cycles over `task-clock`) | `perf stat -d ./app` |
| Scheduler and paging behaviour | `perf stat -e task-clock,context-switches,cpu-migrations,page-faults ./app` |
| Top-down level 1 | `perf stat --topdown ./app` or `-M TopdownL1` ([tutorial 12](12_inside_a_modern_core.md) section 10.3) |
| Where is the time spent (functions, call chains) | `perf record -F 99 -g ./app` then `perf report`; or `perf top` live |
| Which instructions of a hot function | `perf annotate` |
| A flame graph | see below |

The sampling rate of 99 Hz is deliberate: it is not a divisor of the common 100 or 1000 Hz periodic activity, so samples do not fall in step with it. **Call chains need frame pointers**: GCC omits them by default at `-O2`, so stacks are cut short and "towers" go missing; build with `-fno-omit-frame-pointer`, or use DWARF unwinding at a higher cost.

### 8.4 Flame graphs

A **flame graph** (Brendan Gregg) turns thousands of sampled call stacks into one picture. The commands from his page:

```text
perf record -F 99 -a -g -- sleep 60
perf script | ./stackcollapse-perf.pl > out.perf-folded
./flamegraph.pl out.perf-folded > perf.svg
```

How to read it, from the same source: the **y-axis is stack depth** (the caller below, the callee above); each rectangle is a function, and its **width is the share of samples** in which it appeared on the stack; the **x-axis is not time**: stacks are sorted alphabetically so that identical ones merge, so you cannot read order or duration left to right. Look for **wide, flat "plateaus"** at the top: the functions on-CPU most of the time. A function that is wide but has narrow children is the cost itself; one that is wide because of what it calls is a caller. A CPU flame graph shows where the CPU went, not why a request was slow when the thread was blocked (locks, I/O, scheduling delay): for that an off-CPU profile is needed, and the time spent waiting is the subject of the latency sections above.

### 8.5 A method that survives pressure

1. **State the goal as a metric**: "p99 latency under 20 ms at 5,000 requests a second", not "make it faster".
2. **Reproduce it** in a form you can run repeatedly, with the noise measured (section 4.4).
3. **Narrow from the outside in**: system first (the USE method: for each resource, whether CPU, memory, disk or network, check its utilisation, its saturation and its errors), then process, thread, function, line, instruction.
4. **Form a hypothesis from a model** (iron law, roofline, queue, Amdahl) with a number attached, so the experiment can fail.
5. **Change one thing**, re-run the baseline in the same session, compare *distributions*, not single numbers.
6. **Stop when the model and the measurement agree**, or you have exhausted the budget. Write down what you learned; an unexplained win is a regression waiting.

---

## 9. Power and energy

### 9.1 Power, energy and why the distinction matters

**Power** (watts) is the rate of using energy; **energy** (joules, or kilowatt-hours on a bill) is power times time. Power sets what the cooling and the supply must handle at one moment; energy sets the battery life, the electricity bill and, for a task, the true cost. A task that draws twice the power but finishes in a third of the time uses two thirds of the energy. Compare machines on **energy per task** (or work per joule) unless the question is about cooling or provisioning.

### 9.2 The power model: dynamic plus static

[Tutorial 18](18_multicore_and_multithreading.md) section 1.1 gave the dynamic term, `α C V² f`, and the cube law (voltage rising with frequency makes power grow roughly as *f*³). The complete picture adds the static term:

```text
P  =  α C V² f   +   V × I_leak(V, T)
      (switching)     (leakage: paid whenever the circuit is powered, rising with voltage and sharply with temperature)
```

Leakage is the reason Dennard scaling ended (section 1.2) and the reason idling is not free: a core that executes nothing still burns static power unless it is powered down (section 9.5). The two terms behave differently with frequency, which makes the energy to finish a fixed amount of work *non-monotonic*:

```text
energy per cycle  =  P / f  =  α C V(f)²   +   P_static / f
```

The first term grows as the frequency (and with it the voltage) rises; the second falls, because a faster clock spreads the static power over more work. **Worked example** with an *assumed* voltage curve `V = 0.6 + 0.13 f` (volts, *f* in GHz), a constant static power, and units chosen so that the dynamic energy per cycle at 3.0 GHz is 0.98:

| Frequency (GHz) | Voltage (V) | Dynamic energy per cycle | Static energy per cycle | **Total energy per cycle** | Time for the work | Energy × delay |
|---|---|---|---|---|---|---|
| 1.0 | 0.73 | 0.53 | 0.350 | 0.88 | 3.0× | 2.65 |
| **1.3** | 0.77 | 0.59 | 0.269 | **0.86** (minimum) | 2.3× | 1.99 |
| 1.5 | 0.80 | 0.63 | 0.233 | 0.86 | 2.0× | 1.73 |
| 2.0 | 0.86 | 0.74 | 0.175 | 0.92 | 1.5× | 1.37 |
| 2.5 | 0.93 | 0.86 | 0.140 | 1.00 | 1.2× | 1.19 |
| 3.0 | 0.99 | 0.98 | 0.117 | 1.10 | 1.0× | 1.10 |
| 3.5 | 1.06 | 1.11 | 0.100 | 1.21 | 0.86× | 1.04 |

(Energy × delay is the energy per cycle times the relative time, so the 3.0 GHz row is 1.10 × 1.0.) Three readings. **There is an energy-optimal frequency** (1.3 GHz in this model, and the curve is nearly flat from 1.2 to 1.5): above it, each extra gigahertz costs more energy than it saves in leakage; below it, running slower wastes more in leakage than it saves in switching. **Energy and speed pull apart**: from 1.3 to 3.0 GHz energy rises 27 % while the job finishes 2.3 times as fast. And the **energy-delay product**, which punishes slowness, is minimised at the *top* frequency of this table; which metric you optimise is the design decision. The numbers are illustrative; real voltage-frequency curves differ per chip, but the shape (a shallow energy minimum well below the maximum frequency) is the general finding behind "race to idle against slow and steady" (section 9.5).

### 9.3 Frequency control: P-states, turbo and who decides

A **P-state** is a voltage-and-frequency operating point. In Linux the **cpufreq** subsystem picks among them through a **governor**, from the kernel documentation: `performance` (always the highest allowed), `powersave` (the lowest allowed), `userspace` (you set it), `ondemand` and `conservative` (scale with measured load; the second moves in small steps), and `schedutil` (driven by the scheduler's own utilisation estimate, about `1.25 × f₀ × util / max`, so it responds within a scheduling period). On recent Intel parts the `intel_pstate` driver, with **hardware-managed P-states (HWP)**, hands the decision to the processor itself: the OS gives a hint, the **energy-performance preference**, and the hardware chooses. **Turbo** states lie above the highest *sustainable* P-state and, as the documentation says, are not guaranteed to be sustainable: how long and how high depends on the power, current and temperature headroom at that moment. With HWP the minimum, maximum and preference are hints and the hardware picks the actual frequency, so the number the OS shows is not necessarily the one the core runs at; that is why section 2.2 measured it.

None of this is visible to a guest here: there is no `cpufreq` directory, so a program in this VM can neither read nor set the frequency, only measure its effect (the 2.8 to 3.3 GHz drift of section 2.2).

### 9.4 Idle: C-states

When a core has nothing to run, the kernel's **cpuidle** subsystem picks an **idle state** (C-state) in which the core stops fetching and executing. Deeper states power more of the core down and save more, at the cost of a longer wake-up. The documentation defines two numbers per state: the **target residency**, the minimum time the core must stay there for the state to save more energy than a shallower one, and the **exit latency**, the worst time from a wake-up event to the first new instruction. A **governor** predicts how long the core will be idle and picks a state: `menu` is the default on tickless systems and predicts from recent history and the next timer, `teo` (timer events oriented) is an alternative, `ladder` serves non-tickless systems, and `haltpoll` is for guests: it spins for a short while before halting, because a `HLT` in a VM is expensive. The trade-off is the one [tutorial 26](26_interrupts_exceptions_and_dma.md) section 5.2 measured from the other side: a sleeping cross-CPU hand-off took **10 µs** against **56 to 60 ns** for a spinning one, because waking a sleeping CPU is slow (in that VM most of it is the host's wake-up, not a C-state). Latency-critical systems therefore limit the deepest state (or poll, at the price of full power); battery systems allow all of them.

This guest's `cpuidle` driver is `none`: the idle loop just executes `HLT` and the hypervisor decides what happens next ([tutorial 27](27_hardware_virtualization.md) section 6.5), including whether to poll for a short while before descheduling the vCPU.

### 9.5 Race to idle, and power limits

Whether finishing fast and sleeping beats running slowly depends on how much of the power is a floor that goes away only when the whole system sleeps. If the core alone burns the power (the model of section 9.2), running slow and steady near the energy minimum uses less energy. If the awake system also carries a large fixed cost (memory and interconnect clocking, the rest of the board), **racing to idle** (run fast, then sleep) wins, because that cost is paid only while working. Problem C puts numbers on both cases. Racing also loses its edge when idle power stays high or wake-ups are expensive. Interrupt moderation ([tutorial 26](26_interrupts_exceptions_and_dma.md) section 4.4) is the networking form: batching packets lets a core stay asleep longer between bursts at the price of latency.

**Power limits and heat.** A chip has a **thermal design power (TDP)**, a vendor-defined figure for what the cooling must handle (read the vendor's definition; it is a design number, not a measured maximum). Hardware enforces limits through **RAPL** (running average power limit), which Linux exposes under `powercap`: a **long-term** limit (often set near the TDP) and a **short-term** limit allowing brief excursions above it, each over a time window, plus an energy counter in microjoules (`energy_uj`) with *no* instantaneous-power file: power is the difference of two readings divided by the interval. The consequence for measuring anything: **a short benchmark runs in the burst allowance and a long one at the sustained limit**, so a 10 ms test and a ten-minute test can differ by a lot, and a machine that is hot has more leakage and sheds frequency sooner. State the duration.

On this VM I tried the energy counter the kernel offers (`power/energy-psys`, read with `perf_event_open`): over three seconds of a busy loop **it did not move at all**. A hypervisor can expose the counter and fail to implement it. On bare metal use `perf stat -a -e power/energy-pkg/` or a power meter; in a guest, do not trust any energy number you did not verify against a load you know.

### 9.6 Energy efficiency

- **Performance per watt** (flop/s per watt, requests per second per watt) compares machines at similar speed; **energy per task** is the more honest form. At the scale of a building the overhead of cooling and power conversion is reported as **PUE** ([tutorial 29](29_interconnection_networks_and_warehouse_scale_computing.md) section 8.2).
- **Energy-delay product** (E × t) and **E × t²** weight speed more heavily, for when slowness has a cost of its own; section 9.2 shows they favour different frequencies than plain energy.
- **Data movement dominates.** [Tutorial 24](24_accelerators_and_fpgas.md) section 1.1 gave the 45 nm figures: reading a word from DRAM costs 640 pJ, as much as 173 FP32 multiplications, and an 8-bit multiply-accumulate is 15 times cheaper than an FP32 one. So the efficient design keeps data close and narrow: caches and blocking (the roofline of section 7), smaller types, and fixed-function engines, which is the whole case for accelerators and the reason a roofline's left side is also an energy problem.
- **Embedded and battery devices** live on the idle side of the model: the winning pattern is wake, do the work in a burst, return to the deepest allowed state, and avoid wake-ups that do little (a timer tick, a polled register, an interrupt per packet). The same arithmetic as section 9.2 applies, with the static power replaced by the whole board's idle floor.

### 9.7 Switching off what is idle: clock gating, power gating, voltage domains

The two terms of section 9.2 have a hardware answer each. **Clock gating** attacks the dynamic term: an AND gate (in practice a latch plus an AND, so the clock cannot glitch) stops the clock to a block that has nothing to do, so its flip-flops stop switching and *α* falls to nearly zero. It costs almost nothing and is automatic in a modern design: a register that is not being written does not need a clock edge, which is the load-enable of [tutorial 04](04_sequential_logic_and_state_machines.md) section 5 turned into a power saving. **Power gating** attacks the static term: a large transistor between the block and the supply cuts the power completely, so leakage goes to nearly zero too. It is not free. The block loses its state unless the important flip-flops are *retention* cells that keep a copy on a separate supply; the outputs must be clamped by *isolation* cells so the neighbours do not see garbage; and waking up costs both **time** (the supply must ramp, the state must be restored) and **energy**. Separate **voltage domains** let each block run at its own voltage and frequency: cores, caches, the memory controller and I/O are usually separate.

An illustrative block with **assumed** numbers: 100 mW of dynamic power at full activity, 20 mW of leakage, of which the clock network alone is 30 % of the dynamic power, and busy 10 % of the time.

| Idle policy | Power while idle | Average power |
|---|---|---|
| nothing gated (the clock keeps toggling) | 30 mW clock + 20 mW leakage = 50 mW | 0.1 × 120 + 0.9 × 50 = 57 mW |
| clock gated | 20 mW (leakage only) | 0.1 × 120 + 0.9 × 20 = 30 mW |
| clock and power gated | about 0 mW | 0.1 × 120 + 0.9 × 0 = 12 mW |

The last row only holds if the idle stretches are long enough. If waking the block costs 50 µJ, then gating it saves 20 mW of leakage, and the **break-even idle time** is 50 µJ ÷ 20 mW = **2.5 ms**: shorter idle periods lose energy by gating. That is exactly the **target residency** of a C-state in section 9.4, and it is why the kernel's idle governor predicts how long the core will stay idle before choosing a depth.

What this means for software: every wake-up has a cost in energy and latency, so a program that wakes a core every millisecond to do nothing keeps it out of its deep states. Coalesce timers (Linux has timer slack), prefer events to polling when the device is idle, and use the architecture's wait instructions (`wfi` on Arm, `mwait` or `pause` on x86) in idle loops instead of spinning at full activity. In a server the same ideas hold at the scale of whole cores and sockets.

---

## 10. What this means for your code

| Symptom | Likely cause | Try |
|---|---|---|
| A benchmark is slower on its first run, or its time keeps drifting down | warm-up: first-touch page faults (256 MiB took 140 ms the first time and 50 ms the next), cold caches and predictors, a frequency ramp | discard warm-up iterations, run many repetitions, report the distribution (section 4.3) |
| A microbenchmark reports zero, or a rate above the hardware bound (149 Gflop/s on a 96 Gflop/s core) | the compiler deleted or merged the work | consume the result, use a `volatile` input or an empty `asm` barrier, read the assembly, compare with the arithmetic bound (sections 4.1, 4.5) |
| p99 is far above p50 on a machine that is doing nothing | interrupts and, in a VM, the host taking the CPU away: a bare loop here lost 3.4 to 4.2 % of its time in about 3,000 gaps a second | measure the floor with the gap loop (section 6.3); pin and isolate CPUs, use dedicated hosts, and set targets above the platform floor |
| Latency explodes as load approaches 80 to 90 % | queueing: mean time is `service ÷ (1 - ρ)`, so a 1 ms service has a 20 ms mean and a 92 ms p99 at ρ = 0.95 | add headroom or capacity, shed load early, bound queue lengths, cut service-time variance (section 6.5) |
| Throughput falls when more threads are added | contention and coherence traffic: the `β N²` term of the Universal Scalability Law (peak at N = 44 for α = 0.05, β = 0.0005) | shard or batch shared state, remove false sharing, find the knee by measuring, not by assuming (section 5.3) |
| A 10x optimisation gives 1.3x end to end | Amdahl: the part was 25 % of the time; the other 75 % is untouched | profile first, then optimise the largest fraction; recompute the limit after each change (section 5.1) |
| Fast on the laptop, slow in CI or on a cloud VM | different real clock (turbo, power limit), steal time, noisy neighbours, a smaller cache share | compare distributions on the same machine, state frequency and core count, check `st` in `top`, pin |
| Gflop/s is a small fraction of peak | the kernel is left of the ridge (memory-bound) or running scalar | compute AI against the ridge (6.7 flop per byte here); block, fuse, shrink the data type; check vectorisation (section 7) |
| IPC is low and nobody knows why | memory stalls, mispredictions, a starved front end, or a latency chain | CPI stack and top-down (section 8); in a VM without counters, derive IPC from time and the measured clock and use sampling |
| Cycles computed from nanoseconds disagree with a counter | the core clock is not the label: 2.8 to 3.3 GHz measured against 2.1 GHz nominal here, and `rdtsc` ticks at a fixed rate | measure the clock with a dependent add chain (section 2.2); keep "TSC ticks" and "core cycles" apart |
| The average looks fine and users complain | percentiles, and fan-out: 100 backends with a 1 % slow tail make 63 % of requests slow | measure p99 and p99.9 with an open-loop generator; hedge, cap fan-out, shrink the tail at the source (sections 6.2, 6.4, 6.6) |
| `perf stat` shows `<not supported>` for cycles in a VM | the virtual PMU is not exposed | enable it in the hypervisor; meanwhile use software events and time-based sampling (`cpu-clock`), which still give flame graphs |
| Two identical runs differ by 5 to 10 % | noise: frequency, other tenants, placement, interrupts | many runs, median and spread, pinned threads, a fixed input, and a stated duration (section 4.4) |
| A task finishes sooner on the "high performance" setting but the battery drains faster | energy per task is not minimised at the top frequency; short bursts followed by deep idle are cheapest, wake-ups that do little are not | batch work, avoid polling and periodic timers, measure energy (joules per task) and not only time (section 9) |
| A published benchmark says A beats B, but your service does not agree | the benchmark is not your workload; the geometric mean hides the one program that matters to you | run your own workload at your own load, with a tail-latency bound (section 3.5) |

---

## 11. Mistakes that cost points

1. **Quoting the average where the question is about experience.** Averages hide the tail; say p50, p99 and p99.9, and the load at which you measured them.
2. **Averaging percentiles.** The mean of two p99s is not the p99 of the combined traffic. Merge histograms (or keep the raw samples) and compute the percentile of the merged data.
3. **Taking the arithmetic mean of speedups or ratios.** It contradicts itself when you swap the reference machine (section 3.3). Use the geometric mean for ratios, the harmonic mean for equal-work rates, and totals for a fixed job list.
4. **Treating MIPS or peak FLOPS as performance.** MIPS depends on the instruction set and mix; peak FLOPS needs full-width FMA on every unit every cycle. This core measured 1 to 2 Gflop/s on a naive matrix multiply against a 96 Gflop/s peak.
5. **Confusing the nominal clock, the TSC and a core cycle.** On this machine they are 2.1 GHz, 2.1 GHz and about 3.0 GHz. Converting nanoseconds to cycles with the label undercounts the cycles by a third or more.
6. **Timing one short operation with a clock call.** `clock_gettime` costs 30 ns and a single multiply "measured" 31 ns; time a loop of many and divide, or use a harness that does.
7. **Not checking that the work still exists.** Unused results, loop-invariant code and identical independent chains all get removed or merged. Read the assembly, and sanity-check against the hardware bound.
8. **Reporting one run, or the best run.** Give the median and the spread over many runs. The minimum is a legitimate "best case" if labelled, but it is not the typical case.
9. **Benchmarking only the warm state, or only the cold one, without saying so.** Both are real; production often sees the cold start.
10. **Quoting Amdahl's limit without its assumptions.** It assumes a fixed problem and a serial fraction that does not grow; it also does not say that more threads can make things *slower* (contention, the USL), or that a bigger problem changes the picture (Gustafson).
11. **Using a closed-loop load generator for latency.** A generator that waits for each reply before sending the next slows down exactly when the server stalls and omits the requests that would have waited: p99.9 of 1 ms against 992 ms for the same stall (coordinated omission, section 6.6).
12. **Planning to run a service at 90 to 95 % utilisation.** Queueing delay grows without bound as ρ approaches 1; the p99 at 0.95 is already 92 times the service time.
13. **Saying "memory-bound" without a number.** State the arithmetic intensity, the ridge point and the bandwidth you measured; and remember that latency-bound code (pointer chasing) sits far below the bandwidth roof for a different reason.
14. **Reading IPC as good or bad by itself.** A thread spinning on a lock has a fine IPC. Use top-down buckets, or ask what the instructions *are*.
15. **Using the wrong profile.** A CPU flame graph cannot show time spent blocked; stacks cut short because of missing frame pointers produce misleading towers; sampling at 100 Hz can lock-step with a 100 Hz timer.
16. **Comparing power when the question is energy.** A faster chip drawing more watts can use fewer joules per task. Compare energy per task, and say whether the metric is energy, energy-delay or performance per watt.
17. **Assuming the lowest frequency saves the most energy.** Leakage keeps burning while the work takes longer; in the model of section 9.2 the energy minimum is near 1.3 GHz, and 1.0 GHz uses more energy than 1.5.
18. **Taking TDP as the maximum power, or a 10 ms benchmark as sustained performance.** TDP is a cooling design figure; short runs use the turbo allowance, long runs the sustained limit.
19. **Trusting energy, frequency or counter readings inside a VM.** Here the energy counter stayed at zero and the frequency interface does not exist; check any such number against a load you know.

---

## 12. Questions you will be asked

**Q1. State the iron law. What does each term depend on?** `time = instructions × CPI ÷ clock`. The instruction count depends on the algorithm, the compiler and the instruction set; CPI depends on the microarchitecture, the memory system and the program's mix; the clock depends on the process, the design and the power budget. A change helps only if it improves one term without hurting the others by more. Worked example: machine X at 3.5 GHz with CPI 1.2 runs 1.0 × 10⁹ instructions in 0.343 s; machine Y at 2.8 GHz with CPI 0.7 needs 1.15 × 10⁹ instructions (a different instruction set) and finishes in 0.2875 s, faster, despite the lower clock and more instructions. Add the caveat that "the clock" is the *actual* core frequency, which under turbo and power limits is not the number on the box (2.8 to 3.3 GHz measured against 2.1 GHz here).

**Q2. Why did single-thread performance stop doubling every two years?** Moore's law (transistor count) continued, but Dennard scaling ended: shrinking transistors used to cut voltage in proportion, so power per area stayed constant while the clock rose. Around the mid-2000s voltages stopped falling because of leakage, so power density rose with every shrink and with the clock (`P ∝ C V² f`, and *V* has to rise with *f*). Clock speeds flattened near 3 to 5 GHz and the industry went to more cores, wider vectors, accelerators and specialisation. Esmaeilzadeh et al. projected that at 22 nm 21 % of a chip could not be powered at full speed at once, and more than 50 % at 8 nm ("dark silicon"), and that multicore alone would deliver only about 7.9 times average speedup through 2024, a nearly 24-fold gap to the historical target of doubling every generation (a 2011 model projection, not a measurement). Cost also rose: a new node costs more and large dies yield poorly (a 600 mm² die at a defect density of 0.1 per cm² yields about 55 %, against 90 % for 100 mm²; four 150 mm² chiplets give about 1.8 times as many good products per wafer as one big die, in my model).

**Q3. What is wrong with MIPS and peak FLOPS as performance numbers?** MIPS is instructions per second, which depends on how much work an instruction does: a machine with simple instructions can have a higher MIPS and a slower run time. Peak FLOPS is a product of the unit count, the vector width, 2 flops per FMA and the clock, and a real program reaches it only with every unit busy every cycle with data in registers. On this core the peak is 96 Gflop/s per thread; a naive matrix multiply reached 1 to 2 Gflop/s, a loop-reordered one 8, and a tuned library 80 to 85, for the same arithmetic. Neither number says anything about memory, branches or how much of the program is serial. Time to solution on *your* workload is the only metric that does not lie.

**Q4. How do you summarise a benchmark suite, and why the geometric mean?** Compute each program's ratio against a reference machine and take the geometric mean of the ratios (SPEC CPU2017 does exactly this, with the median of three runs, base rules that require identical flags, and a historical reference machine). The geometric mean is the only mean for which the ranking of two machines does not depend on which one is the reference; the arithmetic mean of ratios says "B is 25 % slower" and "A is 25 % slower" for the same data. It is not the right summary for a fixed job list (use total time), for equal-work rates (harmonic mean) or for your own mix (a weighted mean with your weights).

**Q5. A vendor claims "2x faster". What do you ask?** Faster than what (the baseline part and software)? On what workload, and is it mine? Which metric (single-thread speed or throughput; base or peak; mean or tail)? What was held fixed (clock, cores, memory, compiler flags)? At what power? Was it the best of several runs, and can I reproduce it? A 2x on a benchmark that fits in cache says nothing about a service that does not.

**Q6. You are asked to microbenchmark a function. What can go wrong and how do you guard against it?** The compiler can delete the work (a 10⁹-iteration loop took 0.001 ms with its result unused) or merge identical chains (149 Gflop/s on a core whose bound was 96); the timer costs 30 ns and has limited resolution; the first run pays page faults and cold caches; frequency, other tenants and interrupts add noise (60 repeats of a 96 ms kernel ranged from 84 to 105 ms); the hardware may fold the operation away (a dependent add of a constant ran faster than its own latency); and the data and machine state are part of the benchmark. The guards: consume the result, read the assembly, compare with the arithmetic bound, time a batch not a single call, warm up, repeat many times, pin the thread, report median and spread, and vary the input.

**Q7. State Amdahl's law and use it.** With a fraction *p* of the run time sped up by *s*, `speedup = 1 ÷ ((1 - p) + p ÷ s)`; the limit as *s* grows is `1 ÷ (1 - p)`. Example: a request is 10 % parse, 5 % auth, 55 % database, 25 % render and 5 % logging. Making the database 2x faster gives 1.38x; 10x gives 1.98x; infinitely fast gives 2.22x. Making render 10x faster gives only 1.29x. The lesson is to optimise the biggest fraction and to recompute after each change. Limits of the law: it assumes a fixed problem size (Gustafson's view lets the problem grow with the machine, so the serial fraction shrinks as a share), and it ignores the cost of parallelism itself.

**Q8. Why can adding threads reduce throughput?** Amdahl's law has a ceiling, never a decline. Real systems also pay for contention (queueing at a lock) and for coherence (cache lines bouncing between cores). The Universal Scalability Law adds a quadratic term: `C(N) = N ÷ (1 + α(N - 1) + βN(N - 1))`, where α is contention and β is coherence delay. With α = 0.05 and β = 0.0005 the speedup is 10.5 at 32 threads, peaks at 10.7 near 44, and falls to 9.3 at 96. The fix is to shrink what is shared (shard, batch, per-thread state), not to add hardware.

**Q9. Latency against throughput, and what does Little's law say?** Latency is the time for one thing; throughput is how many things per unit time. They trade off: batching raises throughput and raises latency, and a system pushed toward its throughput limit queues. Little's law, `L = λ W` (items in the system equal arrival rate times time in system), holds for any stable system and any distribution. Examples: 200 requests a second at 50 ms each means 10 in flight; a core that must sustain 13.5 GB/s from DRAM at 221 ns latency needs `(13.5 × 10⁹ ÷ 64) × 221 × 10⁻⁹` = 47 cache lines in flight (19 if the latency is 90 ns), against roughly 12 that demand misses can keep outstanding ([tutorial 14](14_cache_optimisation_and_the_real_hierarchy.md)). That gap is why hardware prefetchers, which run deeper queues, and memory-level parallelism matter.

**Q10. Why report p99, and what does fan-out do to it?** The mean describes no user. A user's experience is a draw from the distribution, and a page or API call that touches many backends waits for the slowest. If each backend is slow 1 % of the time, a request that waits for all of 100 backends is slow with probability `1 - 0.99¹⁰⁰` = 63 %. The Tail at Scale paper's example: servers with a 10 ms typical response and a 1 s 99th percentile give 63 % of requests over a second at 100 servers. Mitigations: hedged requests (send a second copy after the 95th percentile and take the first reply), tied requests, smaller work units, micro-partitions, selective replication, and tolerating partial results. Report percentiles of merged data, not an average of percentiles.

**Q11. Why does latency blow up near full utilisation?** Because random arrivals form queues whenever the server is busy, and the average wait grows as `ρ ÷ (1 - ρ)`. For an M/M/1 queue with a 1 ms service time the mean response time is 2 ms at ρ = 0.5, 5 ms at 0.8, 10 ms at 0.9, 20 ms at 0.95 and 100 ms at 0.99, and the 99th percentile is 4.6 times the mean (9.2, 23, 46, 92 and 460 ms). A simulation matched up to ρ = 0.9 and ran 9 % and 20 % high at 0.95, where heavily loaded queues converge slowly. Practical consequences: keep headroom, size for the tail, bound queues, and shed load rather than letting a queue grow.

**Q12. What is coordinated omission?** A load generator that sends the next request only after the previous reply arrives lets a server stall and then *stops sending* during the stall, so the requests that would have queued behind it are never recorded. With a 1 s stall in a 10 s run at 1,000 requests a second, a closed-loop generator reports a p99.9 of 1 ms; an open-loop one, sending on schedule and measuring from the intended send time, reports p90 = 2 ms, p99 = 902 ms and p99.9 = 992 ms. Use a generator with a fixed arrival schedule (wrk2 with `-R`, for example), and measure latency from when the request *should* have been sent.

**Q13. Explain the roofline model and how you build one.** Attainable performance is `min(peak flop/s, bandwidth × arithmetic intensity)`, where intensity is flops per byte moved to or from memory. Build it from two measured numbers: the peak, from a loop of independent fused multiply-adds (86 to 93 Gflop/s for one thread with AVX-512 here, 176 to 182 for two), and the bandwidth, from a triad over arrays larger than the caches (13.5 GB/s for one thread, 26 for two). The ridge point is their ratio: 6.7 flop per byte. A kernel left of the ridge is memory-bound: its bound is bandwidth × intensity (a triad at 0.083 flop per byte gets 1.1 Gflop/s); right of it compute-bound. Caveats: it counts flops, not instructions, so it cannot see integer or branchy code; it assumes bandwidth-bound traffic, so latency-bound code lies far below the roof; and intensity depends on cache behaviour.

**Q14. A program reaches 5 % of peak flops. What do you do?** First place it on the roofline: compute its arithmetic intensity (flops over bytes moved, from the algorithm or from counters) and compare with the ridge. If it is left of the ridge, the fix is more reuse: blocking, fusion, a smaller data type, a better layout. A naive matrix multiply is 1 to 2 Gflop/s and a blocked library 80 to 85, from the same 2n³ flops. If it is right of the ridge, check vectorisation, FMA use and the number of independent chains (a dependent FMA chain gets one FMA per four cycles). If neither explains it, use top-down to find the stall category, and sample with `perf record` to find where. State the hypothesis first, as a number.

**Q15. How do you find where the cycles go, and what if there are no counters?** With counters: `perf stat -d` for IPC, cache and branch miss rates; top-down level 1 to split slots into retiring, bad speculation, front-end bound and back-end bound; descend into the largest; `perf record -F 99 -g` and `perf report` or a flame graph to find the code. Without counters (as in a VM like this one): wall-clock and CPU time, software events, time-based sampling (the `cpu-clock` event gives a profile with no PMU), and derived numbers such as IPC from an instruction count divided by time × measured frequency. Either way the method is the same: state a hypothesis from a model, change one thing, measure the distribution again.

**Q16. How does a flame graph work, and when is it misleading?** `perf` samples the stack at a fixed rate (99 Hz is typical, chosen to avoid lock-step with 100 Hz activity); identical stacks are merged; each box is a function with width proportional to the share of samples that contained it, and the vertical axis is call depth. The horizontal order is alphabetical, not time. Look for wide tops. It misleads when stacks are broken (no frame pointers: build with `-fno-omit-frame-pointer` or use DWARF), when the program is mostly *off* CPU (waiting on locks or I/O needs an off-CPU profile), when inlining merges functions, and in a VM, where time the host takes away shows up in no stack at all.

**Q17. Dynamic against static power, and why is there an energy-optimal frequency?** Dynamic power is `α C V² f`: switching. Static power is leakage, paid whenever the circuit is powered, and rising with voltage and temperature. Energy per cycle is `α C V² + P_static ÷ f`: the first term rises with frequency because voltage must rise with it; the second falls because a faster clock spreads leakage over more cycles. So total energy per unit of work has a minimum at an intermediate frequency (near 1.3 GHz in my illustrative model: 0.86 energy per cycle against 1.10 at 3.0 GHz, 21 % less, at 2.3 times the run time). Running at the minimum is not always right: if time matters (latency, energy-delay product) the optimum moves up, and if the chip can drop to deep idle between jobs, finishing quickly and sleeping ("race to idle") wins.

**Q18. What are P-states, C-states and turbo, and what does TDP mean?** A P-state is a voltage-frequency operating point for a running core; the cpufreq governor (`schedutil`, `ondemand`, `performance`, `powersave`) or, on recent Intel parts, the hardware itself under HWP, picks it. A C-state is an idle state: deeper states save more power but have a longer exit latency, and the governor (`menu`, `teo`) picks one by predicting idle time against each state's target residency. Turbo states lie above the sustainable frequency and last only while power, current and temperature allow. TDP is a vendor figure for what the cooling must handle, not a maximum or a measurement; the chip enforces separate short-term and long-term power limits through RAPL, so a short benchmark runs in the burst allowance and a long one at the sustained limit. In a VM the guest often can see none of this.

**Q19. What are clock gating and power gating, and what does each save?** Clock gating stops the clock to an idle block, so its flip-flops stop switching: it removes the dynamic power of the block (the clock network is a large share of it) at almost no cost. Power gating disconnects the block from the supply, removing leakage as well, but the block loses its state unless retention cells keep it, needs isolation cells on its outputs, and takes time and energy to wake. Gating a block saves energy only if it stays idle longer than the break-even time (wake energy ÷ leakage saved; 50 µJ and 20 mW give 2.5 ms in my example), which is the target residency of a C-state.

**Q20. A service got slower after an upgrade. How do you approach it?** State the metric (p99 at a given load) and reproduce it with an open-loop generator on identical hardware. Check the platform first (frequency, steal time, other tenants, kernel and microcode changes). Compare distributions of old and new, not means. Split by resource (utilisation, saturation, errors of CPU, memory, disk, network). If CPU: flame graphs of old and new, differential if possible; CPI stack or top-down for what the cycles are doing. Form a hypothesis with a number attached ("the new code adds one DRAM miss per request, about 220 ns; at 20 µs per request that predicts 1 % slower, so a 15 % regression is something else"), test it, and stop when the model and the measurement agree.

---

## 13. Problems

**How to use this section.** Four are paper problems with a short script to check them; the last uses a real machine. Each says *Where* to check the answer and *How*. Do the paper part first, then check, and fix the paper answer if they disagree. Time: about 20 to 30 minutes each.

### Problem A (paper, then a script): the iron law, the means and the clock

(a) Machine X runs at 3.5 GHz with CPI 1.2 and executes 1.0 × 10⁹ instructions; machine Y runs at 2.8 GHz with CPI 0.7 and needs 1.15 × 10⁹ instructions for the same program. Find both run times, the speedup of Y over X, and the CPI at which Y would only tie with X.
(b) Run times in seconds of three programs: on machine A, 10, 100 and 1,000; on machine B, 20, 50 and 500. Compute the ratios B/A and A/B, the arithmetic and geometric mean of each set, and the total time on each machine. What does each summary say? Which would you publish for a ranking across a suite, and which for a nightly batch that runs these three?
(c) A 7 µs operation measured with the time-stamp counter is reported as 14,700 cycles. (i) What clock does that assume? (ii) The core was running at 3.0 GHz: how many core cycles did the operation take? (iii) A chain of dependent `imul` instructions (latency 3 cycles) takes 0.98 ns per instruction: what clock does that imply?
(d) A test does the same amount of work in two phases that run at 100 and at 10 items per second. What is the average rate over the whole test?

*Where:* paper; check with the ten-line script in the Answers.
*How:* (a) `time = instructions × CPI ÷ clock`. (b) a ratio of 0.5 and a ratio of 2 are not symmetric under the arithmetic mean. (c) ticks are `time × TSC rate`. (d) average the times, not the rates.

### Problem B (paper, then a script): Amdahl, scalability, Little's law and a queue

(a) A request spends 15 % of its time parsing, 45 % in a lookup, 30 % serialising the reply and 10 % logging. What is the overall speedup if the lookup becomes 4 times faster? Infinitely fast? Which is better, a lookup twice as fast or a serialiser three times as fast? Both together?
(b) A service follows the Universal Scalability Law with α = 0.03 (contention) and β = 0.0002 (coherence). Give the speedup at 8, 16, 32, 64 and 128 threads and the thread count that maximises it. What does the curve say about buying a 128-core machine?
(c) A single-server service has a 5 ms mean service time and random (Poisson) arrivals. At 90 % utilisation, find the arrival rate, the mean response time, the 99th percentile and the mean number of requests in the system (check it with Little's law). What is the highest utilisation, and request rate, at which the 99th percentile stays under 100 ms?
(d) A page needs replies from 50 backends, and each backend is slower than 20 ms one time in a hundred. What fraction of pages is slower than 20 ms? What per-backend percentile must be under 20 ms for 99 % of pages to be?

*Where:* paper; check with a script.
*How:* (a) `1 ÷ Σ(fraction ÷ speedup)`. (b) `C(N) = N ÷ (1 + α(N - 1) + βN(N - 1))`, maximum at `N* = √((1 - α) ÷ β)`. (c) for M/M/1, mean response `S ÷ (1 - ρ)` and 99th percentile `ln(100)` times that. (d) independent events multiply.

### Problem C (paper, then a script): read the counters, place the kernel

This is an *illustrative* output, constructed for this exercise (this VM has no counters to produce a real one):

```text
 Performance counter stats for './app':

          12,000.00 msec task-clock                #    1.000 CPUs utilized
     36,000,000,000      cycles
     18,000,000,000      instructions
      3,600,000,000      branches
        108,000,000      branch-misses
      5,400,000,000      L1-dcache-loads
        270,000,000      L1-dcache-load-misses
         67,500,000      LLC-load-misses
```

(a) Compute the IPC, the CPI, the average clock (cycles ÷ task-clock), the branch miss rate and mispredictions per 1,000 instructions, the L1 miss rate and LLC misses per 1,000 instructions.
(b) Build a CPI stack from these assumptions: base CPI 0.4; a misprediction costs 17 cycles; an L1 miss that hits in L2 costs 14 cycles; an LLC miss costs 250 cycles (take every L1 miss that is not an LLC miss as an L2 hit). How many cycles does each component take, as a share of the 36 billion? How much is unexplained, and what could it be?
(c) What speedup would halving the LLC misses give? Removing every misprediction? What would you run next?
(d) Use the roof of section 7.2 (90 Gflop/s, 13.5 GB/s). Place on the roofline: `daxpy` (`y[i] += a * x[i]`, FP64, arrays far larger than the caches); the three-point stencil `out[i] = c0 * in[i-1] + c1 * in[i] + c2 * in[i+1]` (5 flops, 8 bytes read and 8 written per element, neighbours cached); and a blocked matrix multiply with `b × b` tiles, which loads one tile of A and one of B per tile product (8 bytes per element) and does `2b³` flops with the C tile kept in cache. For the multiply, find the smallest power-of-two `b` that reaches the ridge, and check whether three tiles of that size fit in the 2 MiB L2.
(e) A colleague reports a kernel at 10 Gflop/s with an arithmetic intensity of 0.5 flop per byte on this core. Is that believable?

*Where:* paper; check with the script in the Answers.
*How:* (b) cycles = events × penalty; compare the sum with the measured 36 × 10⁹. (d) `min(peak, bandwidth × AI)`; AI of the tiled multiply is flops over bytes loaded. (e) compare with the roof.

### Problem D (paper, then a script): energy, deadlines and race to idle

Use the model of section 9.2: energy per cycle `E(f) = (0.6 + 0.13 f)² + 0.35 ÷ f` in arbitrary units, with *f* in GHz, so that power is `E(f) × f`.

(a) Compute `E(f)` at 1.0, 1.2, 1.5, 2.0 and 3.0 GHz. Find the frequency that minimises it (set the derivative to zero and solve numerically).
(b) A task takes 10 ms at 3.0 GHz and must finish within 25 ms. The available P-states are 1.2, 1.5, 1.8, 2.4 and 3.0 GHz. Which one uses least energy while meeting the deadline? What if the deadline is 12 ms?
(c) The task repeats every 25 ms. Between tasks the core sleeps at a power of 0.05. Compare the energy per period if the core runs the task at 3.0 GHz and then sleeps, against running it at 1.5 GHz and then sleeping, counting only the core.
(d) Now add a platform power of 2.0 (memory, interconnect, the rest of the board) that is drawn whenever the core is awake and not at all while it sleeps. Repeat (c). For what platform power do the two choices tie?

*Where:* paper; check with a script.
*How:* energy = power × time. In (b) the lowest frequency that meets a deadline is `3.0 × 10 ms ÷ deadline`, then pick from the list.

### Problem E (on your machine): the clock, the tail, the roofs

Use any Linux machine, physical or virtual, with `gcc` and Python with numpy. Run the programs pinned to one CPU with `taskset -c 0`.

(a) **The clock.** Compile the program of section 2.2 and run it ten times. Change `add` to `imul` (start with `a = 1`; `imul` has a latency of 3 cycles) and run it again. Report the nanoseconds per instruction and the frequency each implies, and compare with what `lscpu` prints.
(b) **The tail.** Compile `gaps.c` from the Answers and run it for 5 seconds. How many gaps longer than 1 µs per second, what share of the time, and what are the median, 99th percentile and maximum? Run it again on another CPU.
(c) **The memory roof.** Compile `triad.c` from the Answers and run it for working sets of 24 KiB, 768 KiB and 384 MiB (1,024, 32,768 and 16,777,216 elements). Compare with the cache sizes in `lscpu`.
(d) **The compute roof and the ridge.** Run `mm.py` from the Answers with `OPENBLAS_NUM_THREADS=1` and `=2` (or your thread count). Take the one-thread result as your practical peak and divide it by the DRAM bandwidth of (c) to get the ridge. Where does the triad sit on your roofline, and where does the matrix multiply (arithmetic intensity `n ÷ 12`)?

*Where:* your own machine or VM.
*How:* compare with this VM's results in the Answers. Expect different numbers on a laptop (turbo, a power limit); that difference is the point.

---

## Answers

### Problem A

(a) X: `1.0 × 10⁹ × 1.2 ÷ 3.5 × 10⁹` = **0.343 s**. Y: `1.15 × 10⁹ × 0.7 ÷ 2.8 × 10⁹` = **0.2875 s**. Y is **1.19 times faster** despite the lower clock and the extra 15 % of instructions. Y would tie at `0.343 × 2.8 × 10⁹ ÷ 1.15 × 10⁹` = **CPI 0.835**.

(b) B/A is 2.0, 0.5, 0.5: arithmetic mean **1.00** ("equal"), geometric mean **0.794** ("B is 1.26 times faster"). A/B is 0.5, 2.0, 2.0: arithmetic mean **1.50** ("A is 50 % slower"), geometric mean **1.26**. The arithmetic means contradict each other (1.00 and 1.50 for the same data); the geometric means are reciprocals (0.794 × 1.26 = 1). Totals: A 1,110 s, B 570 s, so B finishes the batch 1.95 times faster. Publish the geometric mean for a suite ranking (it does not depend on which machine is the reference) and the **total time** for the nightly batch (it is the time you pay; the geometric mean hides that program 3 dominates).

(c) (i) 14,700 cycles in 7 µs is exactly 2.1 GHz, the TSC rate: those are **ticks**, not core cycles. (ii) At 3.0 GHz: `7 µs × 3.0 GHz` = **21,000 core cycles**, 43 % more. (iii) 3 cycles in 0.98 ns is **3.06 GHz**.

(d) `2 ÷ (1/100 + 1/10)` = **18.2 per second** (the harmonic mean), not 55.

Check:

```python
from math import prod
tx = 1.0e9 * 1.2 / 3.5e9; ty = 1.15e9 * 0.7 / 2.8e9
print("X %.4f s  Y %.4f s  speedup %.3f  tie CPI %.3f" % (tx, ty, tx / ty, tx * 2.8e9 / 1.15e9))
A = [10, 100, 1000]; B = [20, 50, 500]
ba = [b / a for a, b in zip(A, B)]; ab = [a / b for a, b in zip(A, B)]
am = lambda x: sum(x) / len(x); gm = lambda x: prod(x) ** (1 / len(x))
print("B/A  AM %.3f  GM %.3f" % (am(ba), gm(ba)))
print("A/B  AM %.3f  GM %.3f" % (am(ab), gm(ab)))
print("totals", sum(A), sum(B), "ratio %.2f" % (sum(A) / sum(B)))
print("core cycles %.0f, clock %.2f GHz, harmonic %.1f" % (7e-6 * 3.0e9, 3 / 0.98, 2 / (1 / 100 + 1 / 10)))
```

### Problem B

(a) Lookup 4 times faster: `1 ÷ (0.15 + 0.45/4 + 0.30 + 0.10)` = **1.51**. Infinitely fast: `1 ÷ 0.55` = **1.82**, the ceiling. Lookup twice as fast alone: **1.29**; serialiser three times as fast alone: **1.25**; so the lookup change is slightly better even though it is a smaller speedup, because its share is bigger. Both together: `1 ÷ (0.15 + 0.225 + 0.10 + 0.10)` = **1.74**.

(b) Speedups: 8 threads **6.55**, 16 **10.68**, 32 **15.03**, 64 **17.31**, 128 **15.88**. The maximum is at `N* = √(0.97 ÷ 0.0002)` = **69.6** threads, with a speedup of **17.3**. A 128-core machine is *slower* than a 64-core one for this workload (15.9 against 17.3); money is better spent reducing α and β (less shared state, less coherence traffic) than on cores.

(c) At ρ = 0.9: arrival rate `0.9 ÷ 5 ms` = **180 requests per second**; mean response `5 ÷ 0.1` = **50 ms**; 99th percentile `4.605 × 50` = **230 ms**; mean number in the system `ρ ÷ (1 - ρ)` = **9**, and Little's law gives `180 × 0.050` = 9, as it must. For a 99th percentile of 100 ms: `4.605 × 5 ms ÷ (1 - ρ) ≤ 100 ms` gives `1 - ρ ≥ 0.23`, so **ρ ≤ 0.77, or 154 requests per second**: about 86 % of the 180 that 90 % utilisation would allow, which is the price of the latency target.

(d) `1 - 0.99⁵⁰` = **39.5 %** of pages. For 99 % of pages to be fast, each backend must be fast with probability `0.99^(1/50)` = 0.99980: the **99.98th percentile** of each backend must be under 20 ms. This is why tail-tolerant systems hedge requests.

Check:

```python
from math import log, sqrt
sp = lambda parts: 1 / sum(f / s for f, s in parts)
print("lookup 4x %.3f  inf %.3f  lookup 2x %.3f  serialise 3x %.3f  both %.3f" % (
    sp([(.15, 1), (.45, 4), (.30, 1), (.10, 1)]), 1 / .55,
    sp([(.15, 1), (.45, 2), (.30, 1), (.10, 1)]), sp([(.15, 1), (.45, 1), (.30, 3), (.10, 1)]),
    sp([(.15, 1), (.45, 2), (.30, 3), (.10, 1)])))
a, b = 0.03, 0.0002
C = lambda N: N / (1 + a * (N - 1) + b * N * (N - 1))
print([round(C(N), 2) for N in (8, 16, 32, 64, 128)], "N* %.1f  C(N*) %.2f" % (sqrt((1 - a) / b), C(sqrt((1 - a) / b))))
S = 0.005; rho = 0.9
print("lambda %.0f  mean %.0f ms  p99 %.0f ms  L %.0f" % (rho / S, S / (1 - rho) * 1e3, log(100) * S / (1 - rho) * 1e3, rho / (1 - rho)))
r = 1 - log(100) * S / 0.1; print("max rho %.3f  rate %.0f/s" % (r, r / S))
print("pages slow %.3f  per-backend quantile %.5f" % (1 - 0.99 ** 50, 0.99 ** (1 / 50)))
```

### Problem C

(a) IPC `18 ÷ 36` = **0.50**, CPI **2.0**. Average clock `36 × 10⁹ cycles ÷ 12 s` = **3.0 GHz** (a program that had run at the 2.1 GHz label would show only 25.2 × 10⁹ cycles). Branches: 108 M of 3,600 M is **3.0 %**, which is **6 per 1,000 instructions**. L1: 270 M of 5,400 M is **5.0 %**. LLC misses: 67.5 M over 18 G instructions is **3.75 per 1,000 instructions**, which is a lot.

(b) Cycles:

| Component | Calculation | Cycles (10⁹) | Share of 36 × 10⁹ |
|---|---|---|---|
| Base | 0.4 × 18 × 10⁹ | 7.2 | 20 % |
| Mispredictions | 108 M × 17 | 1.8 | 5 % |
| L1 misses that hit in L2 | (270 M - 67.5 M) × 14 | 2.8 | 8 % |
| LLC misses | 67.5 M × 250 | 16.9 | **47 %** |
| Sum | | 28.7 | 80 % |
| **Unexplained** | | **7.3** | **20 %** |

The unexplained fifth could be data-TLB misses and page walks (not counted above), front-end stalls, longer-than-assumed LLC penalties (the real DRAM latency on this machine is about 660 cycles including a TLB miss, not 250), or errors of the additive model itself. The stack tells you where to look; it does not close the books.

(c) Halving the LLC misses saves `16.9 ÷ 2` = 8.4 × 10⁹ cycles: `36 ÷ 27.6` = **1.31 times faster**. Removing every misprediction saves 1.8 × 10⁹: **1.05 times**. The next step is a profile of where the LLC misses come from (`perf record -e LLC-load-misses`, or the default cycles sampling with `perf annotate`), a look at the data structure being walked, and `dTLB-load-misses` and a top-down breakdown for the unexplained 20 %.

(d) The ridge is `90 ÷ 13.5` = **6.7 flop per byte**. `daxpy`: 2 flops per 24 bytes (read `x` and `y`, write `y`) = 0.083, bound **1.1 Gflop/s**, 1.2 % of peak. Stencil: `5 ÷ 16` = 0.3125, bound **4.2 Gflop/s**, 4.7 % of peak. Tiled multiply: `2b³ ÷ 16b²` = **b ÷ 8** flop per byte, so `b = 32` gives 4 (bound 54 Gflop/s, memory-bound) and `b = 64` gives 8 (above the ridge, compute-bound at 90). The smallest power of two is **64**; three 64 × 64 tiles of doubles take `3 × 64 × 64 × 8` = 96 KiB, which fits the 2 MiB L2 easily (but not the 48 KiB L1, so a real library tiles again for L1 and registers).

(e) No. At 0.5 flop per byte the roof is `13.5 × 0.5` = 6.75 Gflop/s, so 10 is above the bound. Either the intensity is wrong (some of the bytes came from the cache rather than DRAM, so the real DRAM intensity is higher), or the problem fits in cache, or the flops were counted twice. Re-measure the DRAM traffic with counters or at a size well beyond the caches.

Check:

```python
ins, cyc = 18e9, 36e9
base, br, l2, dram = 0.4 * ins, 108e6 * 17, (270e6 - 67.5e6) * 14, 67.5e6 * 250
for n, v in (("base", base), ("mispredict", br), ("L2 hit", l2), ("LLC miss", dram)): print("%-10s %5.1f G  %4.1f%%" % (n, v / 1e9, 100 * v / cyc))
print("unexplained %.1f G (%.0f%%)" % ((cyc - base - br - l2 - dram) / 1e9, 100 * (cyc - base - br - l2 - dram) / cyc))
print("halve LLC misses %.2fx  no mispredicts %.2fx" % (cyc / (cyc - dram / 2), cyc / (cyc - br)))
peak, bw = 90.0, 13.5
print("ridge %.1f  daxpy %.2f  stencil %.2f" % (peak / bw, bw * 2 / 24, bw * 5 / 16))
for b in (32, 64): print("b", b, "AI", b / 8, "bound %.0f" % min(peak, bw * b / 8), "tiles %.0f KiB" % (3 * b * b * 8 / 1024))
```

### Problem D

(a) `E(f)`: **0.883** at 1.0 GHz, **0.863** at 1.2, **0.865** at 1.5, **0.915** at 2.0, **1.097** at 3.0. The derivative `2 × 0.13 × (0.6 + 0.13 f) - 0.35 ÷ f²` is zero at **f = 1.32 GHz**, where `E` = 0.8605: the curve is nearly flat from 1.2 to 1.5, and 3.0 GHz costs 27 % more per cycle than the minimum.

(b) A 25 ms deadline needs `f ≥ 3.0 × 10 ÷ 25` = 1.2 GHz. Among the P-states, 1.2 GHz has the lowest energy (0.863) but finishes exactly at the deadline with no slack; 1.5 GHz costs 0.2 % more (0.865) and finishes at 20 ms. A sensible choice is **1.5 GHz**: the energy difference is negligible and 5 ms of slack absorbs jitter. For a 12 ms deadline the task needs `f ≥ 2.5` GHz; 2.4 GHz would take 12.5 ms and miss, so **only 3.0 GHz** qualifies, at 1.097 per cycle, 27 % above the minimum. The deadline, not the energy curve, decides.

(c) Core power is `E(f) × f`: **3.29** at 3.0 GHz and **1.30** at 1.5 GHz. At 3.0 GHz: `3.29 × 10 + 0.05 × 15` = **33.7**. At 1.5 GHz (20 ms awake, 5 ms asleep): `1.30 × 20 + 0.05 × 5` = **26.2**. Running slowly wins by **22 %**, because the core alone is cheapest near the energy minimum and its idle power is small.

(d) With the platform power: 3.0 GHz gives `(3.29 + 2.0) × 10 + 0.75` = **53.7**; 1.5 GHz gives `(1.30 + 2.0) × 20 + 0.25` = **66.2**. Now racing to idle wins by **19 %**, because the platform power is paid for twice as long when running slowly. The two tie when `(1.30 + P) × 20 + 0.25 = (3.29 + P) × 10 + 0.75`, which gives **P = 0.74**: when the always-on floor exceeds about 23 % of the core's power at 3.0 GHz, race to idle wins. This is why phones and servers differ in practice: the answer depends on how much of the machine can be switched off while idle.

Check:

```python
E = lambda f: (0.6 + 0.13 * f) ** 2 + 0.35 / f
print([round(E(f), 3) for f in (1.0, 1.2, 1.5, 2.0, 3.0)])
lo, hi = 0.5, 3.0
for _ in range(60):                      # bisection on the derivative
    m = (lo + hi) / 2
    if 2 * 0.13 * (0.6 + 0.13 * m) - 0.35 / m ** 2 > 0: hi = m
    else: lo = m
print("f_min %.3f  E %.4f" % (lo, E(lo)))
P = lambda f: E(f) * f
def total(f, plat, work3=10, period=25, idle=0.05):
    t = work3 * 3.0 / f
    return (P(f) + plat) * t + idle * (period - t)
for plat in (0.0, 2.0): print("platform", plat, "3.0 GHz %.1f  1.5 GHz %.1f" % (total(3.0, plat), total(1.5, plat)))
print("tie at platform power %.2f" % ((26.21 - 33.65) / (10 - 20)))
```

### Problem E

Results from this tutorial's guest (a Xeon, two vCPUs, 3 GHz class core):

(a) Dependent `add`: **0.307, 0.319 and 0.308 ns** in three runs, which is **3.13 to 3.26 GHz** (about twenty earlier runs on both CPUs: 0.31 to 0.36 ns, 2.8 to 3.3 GHz). Dependent `imul`: **0.950, 0.961 and 0.964 ns**, which at 3 cycles each is **3.11 to 3.16 GHz**. `lscpu` says 2.10 GHz. The two instructions agree with each other and disagree with the label, which is the evidence that the core is running about 1.5 times faster than the TSC.

(b) The program:

```c
// gaps.c: gcc -O2 gaps.c -o gaps && taskset -c 0 ./gaps 5
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static long now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000L + t.tv_nsec; }
static int cmp(const void *a, const void *b) { long x = *(long *)a, y = *(long *)b; return (x > y) - (x < y); }
int main(int argc, char **argv) {
    long secs = argc > 1 ? atol(argv[1]) : 5, cap = 1 << 20, n = 0, lost = 0;
    long *gap = malloc(cap * sizeof *gap), start = now(), prev = start, t;
    while ((t = now()) - start < secs * 1000000000L) {
        long g = t - prev; prev = t;
        if (g > 1000 && n < cap) { gap[n++] = g; lost += g; }      // anything over 1 us was not us
    }
    if (!n) { puts("no gaps"); return 0; }
    qsort(gap, n, sizeof *gap, cmp);
    printf("%ld gaps > 1 us (%.0f per s), %.2f%% of the time; median %.1f us, p99 %.1f us, max %.1f us\n",
           n, (double)n / secs, 100.0 * lost / (secs * 1e9), gap[n / 2] / 1e3, gap[(long)(n * 0.99)] / 1e3, gap[n - 1] / 1e3);
    return 0;
}
```

Two 5-second runs here: **15,303 gaps (3,061 per second), 4.70 % of the time, median 6.4 µs, p99 69.0 µs, max 4.1 ms**; and 15,452 gaps (3,090 per second), 3.87 %, median 7.9 µs, p99 56.5 µs, max 2.1 ms. A loop that does nothing is interrupted about 3,000 times a second; about 250 of those are the guest's own timer tick and the rest are the host (section 6.3). On a bare-metal machine with isolated CPUs the count is far lower. The loop's own iteration costs about 30 ns, which is the resolution of the measurement.

(c) The program:

```c
// triad.c: gcc -O3 -march=native triad.c -o triad && taskset -c 0 ./triad 16777216
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
int main(int argc, char **argv) {
    long n = atol(argv[1]), reps = 400000000L / n + 1;
    double *a = aligned_alloc(64, n * 8), *b = aligned_alloc(64, n * 8), *c = aligned_alloc(64, n * 8), best = 1e9;
    for (long i = 0; i < n; i++) { a[i] = 0; b[i] = 1; c[i] = 2; }          // first touch outside the timing
    for (int r = 0; r < 5; r++) {
        double t0 = now();
        for (long k = 0; k < reps; k++) {
            double s = 3.0 + k * 1e-9;                                      // changes every pass, so no pass is redundant
            for (long i = 0; i < n; i++) a[i] = b[i] + s * c[i];
            __asm__ volatile("" ::: "memory");                         // keeps the compiler from fusing or interchanging passes
        }
        double t = (now() - t0) / reps;
        if (t < best) best = t;
    }
    printf("%ld elements, working set %.0f KiB: %.1f GB/s (24 B per element), %.2f Gflop/s\n",
           n, 3.0 * n * 8 / 1024, 24.0 * n / best / 1e9, 2.0 * n / best / 1e9);
    return a[n / 2] > 0 ? 0 : 1;
}
```

Here: 24 KiB **235 to 246 GB/s**; 768 KiB **109 to 115 GB/s**; 384 MiB **12.4 to 13.0 GB/s** (a little under the 13.5 of section 7.2 because this program averages over passes instead of taking the best single pass). The three plateaus are L1, L2 and DRAM. Without the `asm` line, `-O3` reported 61 GB/s at 384 MiB: the loop-interchange trap of section 4.5.

(d) The program:

```python
# mm.py: OPENBLAS_NUM_THREADS=1 python3 mm.py
import numpy as np, time
n = 2048; a = np.random.rand(n, n); b = np.random.rand(n, n)
a @ b                                                   # warm-up
t = []
for _ in range(5):
    t0 = time.perf_counter(); a @ b; t.append(time.perf_counter() - t0)
print("%.1f Gflop/s (best of 5, n=%d)" % (2 * n**3 / min(t) / 1e9, n))
```

Here: one thread **81.8 and 85.0 Gflop/s**; two threads **169 and 156 Gflop/s**. With a practical peak of 85 Gflop/s and the DRAM bandwidth of 13 GB/s the ridge is `85 ÷ 13` = **6.5 flop per byte** (section 7.2 gave 6.7 from a peak of 90 and 13.5 GB/s). The triad (0.083 flop per byte) sits far to the left: 1.1 Gflop/s, **1.3 %** of that peak, limited by memory. The matrix multiply has `n ÷ 12` = **171 flop per byte** at n = 2,048, far to the right of the ridge, and the library reaches about 90 % of the peak; the same arithmetic written as a naive triple loop reached 1 to 2 Gflop/s (section 7.3).

---

## Cheat sheet

- **Iron law:** `time = instructions × CPI ÷ clock`. The clock is the *actual* core clock, not the label: here 2.8 to 3.3 GHz against 2.1 GHz (TSC and label). The TSC ticks at a fixed rate; `perf` `cycles` are core cycles and `ref-cycles` are TSC rate; their ratio is the average clock. Measure the clock with a dependent chain: `add` 1 cycle (0.31 ns), `imul` 3 cycles (0.98 ns), 512-bit FMA about 4 cycles; 8 independent chains reach about 4 adds per cycle; a dependent `add` of a constant can beat its latency (rename-time folding).
- **Why performance stopped being free:** Moore's law counts transistors, not speed. Dennard scaling (voltage scales with size, power density constant) ended around the mid-2000s because of leakage; `P = α C V² f` plus static, and `P ∝ f³` over the range where *V* follows *f*. Dark silicon (Esmaeilzadeh 2011): 21 % of a chip dark at 22 nm, over 50 % at 8 nm, about 7.9 times speedup by 2024, a nearly 24-fold gap to the historical trend (a projection). Yield (Poisson, assumed D = 0.1 per cm²): 100 mm² 90 %, 150 mm² 86 %, 600 mm² 55 %; four 150 mm² chiplets give about 1.8 times the good products of one 600 mm² die.
- **Summaries:** geometric mean for ratios (reference-independent; SPEC), arithmetic mean/total for times, harmonic mean for equal-work rates, weights from your workload. Harmonic of 100 and 10 per second is 18.2. SPEC CPU2017: speed and rate, integer and floating point (10, 10, 10, 13 programs), ratio against a Sun Fire V490 reference, median of 3 runs, base against peak. MLPerf Inference: single-stream (p90), multistream, **server** (QPS under a p99 bound), offline.
- **Peak FLOPS and MIPS mislead:** peak = units × lanes × 2 × clock (96 Gflop/s per thread here); naive matrix multiply 1 to 2, loop-reordered 7 to 8, library 80 to 85.
- **Microbenchmark checklist:** consume the result, read the assembly, sanity-bound the number, time a batch (timer cost 30 ns, `rdtsc` 13 ns, resolution), warm up (first touch of 256 MiB 140 ms against 50 ms), many repeats and the distribution (60 runs: 84 to 105 ms), pin, vary inputs, state duration. Traps seen: dead code (10⁹ iterations in 0.001 ms), merged FMA chains (149 against a 96 bound), loop interchange (61 GB/s against 13), strict-FP sum (6.6 against 12 GB/s), add-immediate folding.
- **Amdahl:** `1 ÷ ((1 - p) + p ÷ s)`, limit `1 ÷ (1 - p)`. Example profile: database 55 % made 10 times faster gives 1.98, infinitely fast 2.22. Gustafson: scale the problem. **USL:** `N ÷ (1 + α(N - 1) + βN(N - 1))`, peak at `√((1 - α) ÷ β)` (44 for α = 0.05, β = 0.0005); beyond the peak more threads is slower. Offload: transfer is serial (20x GPU on 70 % of the work: 2.99, 2.53, 1.36 with 0, 6 and 40 % transfer).
- **Latency and the tail:** report p50, p99, p99.9 at a stated load; never average percentiles. A request that waits for N backends with a 1 % slow tail is slow with probability `1 - 0.99^N` (9.6 % at 10, 63 % at 100). Mitigate with hedged and tied requests, smaller units, tolerating partial results. Measured floor in this VM: about 3,000 interruptions a second, 3.4 to 4.2 % of the time lost, maxima of milliseconds.
- **Little and queues:** `L = λW`; bandwidth-delay: 25 Gb/s × 100 µs = 312,500 B; 13.5 GB/s at 221 ns needs 47 lines in flight. M/M/1: mean `S ÷ (1 - ρ)`, p99 4.6 times that; for S = 1 ms, ρ = 0.5, 0.8, 0.9, 0.95, 0.99 give means 2, 5, 10, 20, 100 ms and p99 9.2, 23, 46, 92, 460 ms. Keep latency-sensitive services below 60 to 70 % utilisation.
- **Coordinated omission:** a closed-loop generator hides stalls (p99.9 1 ms against 992 ms for the same 1 s stall). Use a fixed arrival schedule (wrk2 `-R`) and measure from the intended send time.
- **Roofline:** `min(peak, bandwidth × AI)`; this core: 86 to 93 Gflop/s per thread (176 to 182 for two), AVX2 half; scalar ceiling 12; triad 13.5 GB/s (26 for two), L2 105 to 122, L1 230 to 246; ridge 6.7 flop per byte. Triad AI 0.083 (1.1 Gflop/s), SpMV at most 0.17, tiled multiply `b ÷ 8`, ideal multiply `n ÷ 12`. The roofline does not see latency-bound code (pointer chase 0.29 GB/s).
- **Where the cycles go:** CPI stack = base + branch + cache + memory (additive models overstate; OoO overlaps). Top-down: retiring, bad speculation, front-end bound, back-end bound ([tutorial 12](12_inside_a_modern_core.md)). `perf stat -d`, `perf record -F 99 -g`, `perf report`, `perf annotate`, `perf top`; build with `-fno-omit-frame-pointer`; flame graph: width is share of samples, x axis is alphabetical, not time; CPU flame graphs miss off-CPU time. USE method for resources. No PMU in a VM: use `cpu-clock` sampling.
- **Power and energy:** `P = α C V² f + V I_leak`; energy per cycle `α C V² + P_static ÷ f` has a minimum at an intermediate frequency (1.3 GHz in the model); energy × delay favours speed; deadlines can force the top frequency. Race to idle wins when the awake platform adds a large always-on cost (break-even at about 23 % of the core's power at 3.0 GHz in Problem D). P-states (cpufreq governors: `performance`, `powersave`, `ondemand`, `conservative`, `schedutil`; HWP on Intel), turbo (not guaranteed), C-states (target residency, exit latency; `menu`, `teo`, `ladder`, `haltpoll`), TDP (a cooling figure), RAPL short-term and long-term limits (`energy_uj`, no instantaneous power), burst against sustained. Data movement dominates energy: DRAM read 640 pJ against 3.7 pJ for an FP32 multiply (45 nm, [tutorial 24](24_accelerators_and_fpgas.md)). Clock gating removes the dynamic power of an idle block; power gating removes its leakage too, but needs retention and isolation cells and pays wake-up energy, so it pays only when the idle time exceeds wake energy ÷ leakage saved (section 9.7).
- **Everything here was either measured in this VM with short programs (clock chains, timer cost, warm-up, noise, the gap loop, triad bandwidths, peak FMA and matrix-multiply rates), computed by scripts (yield, means, Amdahl, USL, queues, Little's law, the energy model), taken from published sources and kernel documentation (SPEC, MLPerf, the Tail at Scale, flame graphs, dark silicon, cpufreq, cpuidle, powercap), or labelled as an assumed model (yield, CPI stack, DVFS curve); nothing was run with hardware performance counters, a power meter or frequency control, because this guest has none, so the top-down, `perf` and power sections describe the method and the published behaviour, not output from this machine.**

**Next:** tutorial 29, Interconnection networks and warehouse-scale computing (optional: topologies and bisection bandwidth, routing and deadlock, flow control, networks on chip, racks, PUE and tail latency). Say "next" for it, or "skip" to go to tutorial 30, Hardware security.
