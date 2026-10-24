# CO 12: Inside a Modern Core

*Part of the [Computer Organization plan](00_plan.md) · Core tutorial · Time: about 4 hours (2 reading, 2 problems) · You need: [tutorial 09](09_pipelining_and_hazards.md) (pipeline and hazards), [tutorial 10](10_branch_prediction.md) (prediction) and [tutorial 11](11_superscalar_and_out_of_order_execution.md) (out-of-order execution)*

By the end of this tutorial you can:

- draw a modern out-of-order core from fetch to retire, and say what each stage can limit;
- explain decode width, the micro-op cache and macro-fusion, and why x86 and Arm front ends differ;
- explain issue ports, latency and throughput, and the rule "independent operations needed = latency x throughput";
- describe the load/store unit and the L1 data cache, and say when store-to-load forwarding fails;
- compare recent Intel, AMD, Apple and Arm cores from a table, and say what the numbers mean for your code;
- read an instruction table (uops.info, Agner Fog, Arm's guides);
- find the bottleneck of a loop by computing four bounds: rename, ports, dependence chain and memory;
- use the top-down method and read a `perf stat` breakdown.

Tutorials 09 to 11 explained the parts one at a time. This tutorial puts them into one core, with real numbers, and gives you a method for the question you will be asked most: **"why is this loop not faster?"**

**Where the numbers come from.** The core sizes in section 7 are from published articles and manuals (listed under the table). Timings marked *measured* were taken on a cloud virtual machine at about 3.3 GHz, with GCC 13 and `-O2 -fno-tree-vectorize`, so that scalar code stays scalar. The VM reports a Cascade Lake Xeon (a Skylake-family core), but probes show a somewhat newer core underneath (tutorial 13 finds a 48 KB L1 where a Skylake has 32 KB). The limits measured here (FP add latency 4, one multiplier, two FP ports, one store per cycle) are those of a Skylake core, so the lessons hold; the tutorial uses Skylake numbers as its model. The pattern will be the same on your machine; the exact numbers will not. That virtual machine had no performance counters (common in the cloud), so the top-down numbers in section 10 are worked from the formulas, not measured.

> **The one-sentence model.** A core is a chain of queues, each with a width. The program runs at the speed of the narrowest stage, and the structures in between decide how often it gets there.

---

## 1. The whole core in one picture

```text
FRONT END (in order)      branch predictor ─▶ fetch ─▶ decode ─▶ micro-op queue
                          (runs ahead)                  └─ micro-op cache (x86) ─┘
                                                                 │
RENAME / ALLOCATE         W uops per cycle: rename registers; take a ROB entry, a scheduler
(in order)                entry, and a load- or store-queue entry
                                                                 │
SCHEDULER                 waiting uops; a uop wakes up when its operands arrive;
(out of order)            each cycle the ready ones are sent to free ports
                                                                 │
PORTS and EXECUTION       integer ALU · multiply · divide · FP/SIMD · branch ·
UNITS                     address generation (AGU)
                                                                 │
LOAD/STORE UNIT           load queue · store queue · L1 data cache ─▶ L2 ─▶ ...
                                                                 │
RETIRE (in order)         commit from the head of the ROB; free registers;
                          stores drain to the L1 cache
```

**Micro-ops.** An x86 instruction is split into one or more **micro-ops** (uops); most take one. Arm instructions mostly map one to one. The core renames, schedules and retires uops, not instructions. Two kinds of fusion keep the count down:

- **Macro-fusion:** a `cmp` or `test` followed by a conditional jump becomes **one** uop.
- **Micro-fusion:** `addsd xmm0, [rdi]` (an add with a memory operand) is **one** uop up to the scheduler and **two** at execution (a load and an add). A store (address plus data) is also one.

The count at rename is called the **fused-domain** count, and it is the one that meets the width W. Ports are used by the *unfused* parts. This difference matters in section 9.

**What can limit the speed, stage by stage:**

| Stage | What limits it | Where explained |
|---|---|---|
| Fetch, decode | bytes per cycle, decode width, branch-predictor misses | section 2, tutorial 10 |
| Rename | width W (uops per cycle) | section 3 |
| The window | ROB, scheduler, physical registers, load/store queues | section 3, tutorial 11 |
| Execution | number of ports, latency of each unit | section 4 |
| Memory | cache hits and misses, MSHRs | section 5, tutorials 13 and 14 |
| Retire | width R (at least W) | section 6 |

The steady speed can never exceed the narrowest stage.

---

## 2. The front end

### 2.1 The predictor leads

The branch predictor (tutorial 10) runs **ahead of** fetch. It produces a stream of addresses, and fetch follows it. While the prediction is right, the front end delivers a steady stream of uops. A wrong guess, or a taken branch the BTB does not know, breaks the stream and the pipeline refills: 15 to 20 cycles, as tutorial 10 showed. A core can follow only one or two *taken* branches per cycle, so very branchy code is limited here before any other stage.

### 2.2 Fetch and decode

Fetch pulls a block of bytes (16 to 64 per cycle) from the L1 instruction cache (32 to 64 KB; Apple's Firestorm core has 192 KB). **Decode** turns the instructions into uops.

- **Arm (AArch64):** every instruction is 4 bytes. With 4-byte pieces, ten decoders can each take the next piece: no need to find where an instruction starts. The Cortex-X925 decodes 10 instructions per cycle, and Apple's Firestorm 8.
- **x86:** instructions are 1 to 15 bytes long, so the decoder must first find where each one starts. That is hard to do in parallel, which is why x86 decode is harder to widen: Skylake decodes 4 per cycle, Zen 5 uses two 4-wide clusters, and Lion Cove has 8 decoders.
- **Complex x86 instructions** (`rep movs`, some string and system instructions) are fetched from a **microcode ROM**, which delivers only about 4 uops per cycle. Keep them out of hot loops.

This is the real front-end difference between x86 and Arm, far more than "RISC against CISC".

### 2.3 The micro-op cache

x86 cores keep recently decoded uops in a small cache indexed by address (the **micro-op cache**, or op cache). On a hit, decode is skipped: more uops per cycle and less power. Skylake has 1,536 entries delivering 6 uops per cycle; Zen 5 has 6,000 entries with two 6-wide reads per cycle; Lion Cove can deliver up to 12 uops per cycle. A hot loop that fits stays in the micro-op cache. A large, flat code base (a router control plane, a browser, a big server) often does not fit, and then runs from the slower decoders. The Cortex-X925 has no such cache: with a fixed-width instruction set, decoding 10 per cycle is cheap enough.

Some cores add a **loop buffer**: a very small loop is replayed from a tiny queue of uops with no fetch and no decode at all. It exists on some cores, and some parts ship with it switched off; treat it as a bonus, not something to design for.

### 2.4 What starves a front end

| Cause | Typical fix |
|---|---|
| Instruction-cache miss (big code footprint) | profile-guided optimisation (PGO) and link ordering, so hot code sits together; smaller hot paths |
| BTB miss or misprediction | tutorial 10: predictable branches, `likely` hints, branchless code |
| Micro-op cache miss (code too large) | keep hot loops small; do not inline cold paths |
| Microcode instructions | use another instruction or sequence |
| Too many taken branches | lay out the likely path as the straight-line path |

---

## 3. Rename and allocate: where the width lives

Each cycle, the rename stage takes up to **W** uops in program order. It renames their registers (tutorial 11, section 5), and it **allocates** what each uop needs for its life in the window:

| Resource | Needed by |
|---|---|
| ROB entry | every uop |
| scheduler entry | every uop that must execute |
| physical register | every uop that writes a register |
| load-queue entry | every load |
| store-queue entry | every store |

If any of these is full, allocation stops. Which one fills first depends on the code:

- **ROB full:** a long miss sits at the head and the core ran ahead until the window was used up (tutorial 11, section 8).
- **Scheduler full:** many uops wait on a slow dependence chain; the ROB still has room.
- **Physical registers full:** many values alive at once; the free list is empty.
- **Store queue full:** a burst of stores (a large `memcpy` or struct initialisation).

All four show up as "back-end bound" in section 10.

**Free work at rename.** A **zeroing idiom** (`xor eax, eax`) and a register-to-register `mov` are handled at rename (**move elimination**): they take a rename slot but no port and no latency. A `nop` is also free after rename. This is why compilers write `xor eax, eax`.

**Width is a ceiling, not a speed.** Recent cores are 8 to 10 wide, and real programs usually reach an IPC of only 1 to 3, because they do not offer enough independent work (tutorial 11, section 9). Hot arithmetic loops can get close to the width; pointer-chasing code is far below 1.

---

## 4. Schedulers, ports and execution units

### 4.1 The scheduler

The scheduler holds the uops that have been allocated but not yet sent to a unit. Each cycle it picks the oldest ready uops and sends at most one to each port. Its size is the number of waiting uops the core can look through. Skylake has 97 entries in two parts (58 for arithmetic and store data, 39 for address generation); Zen 5 has 88 for integer, 58 for addresses and about 114 for FP/vector.

### 4.2 Ports

A **port** is the doorway to a group of units; each port accepts one uop per cycle. A Skylake core has eight (the units used in the measurements below behave the same on the VM):

| Port | What it executes |
|---|---|
| p0 | integer ALU, FP add / multiply / FMA, vector operations, divider |
| p1 | integer ALU, **integer multiply**, FP add / multiply / FMA, vector operations |
| p5 | integer ALU, vector shuffles and adds |
| p6 | integer ALU, branches |
| p2, p3 | loads (and address generation) |
| p4 | store data |
| p7 | store address |

Three facts follow, and the measurements below confirm them: only **one** integer multiply per cycle (p1), **two** FP adds or multiplies per cycle (p0, p1), and **one** store per cycle.

### 4.3 Latency and throughput

An instruction table gives two numbers per instruction:

- **Latency:** cycles from "inputs ready" to "output ready". It matters when the next instruction needs the result.
- **Reciprocal throughput:** cycles between the starts of independent copies. A value of 0.5 means two per cycle. It matters when instructions are independent.

Real rows (measured by [uops.info](https://uops.info), latency / reciprocal throughput):

| Instruction | Skylake | Golden Cove (Alder Lake-P) | Zen 4 |
|---|---|---|---|
| `add r64, r64` | 1 / 0.25 | 1 / 0.20 | 1 / 0.25 |
| `imul r64, r64` | 3 / 1.0 | 3 / 1.0 | 3 / 1.0 |
| `vaddpd ymm, ymm, ymm` | 4 / 0.5 | 2 / 0.5 | 3 / 0.5 |

An integer add executes 4 to 5 times per cycle; a multiply once per cycle on all three cores; and the FP add latency is 4, 2 or 3 depending on the core. **The same code is limited by a different latency on each machine.**

### 4.4 How much parallelism a unit needs

A unit that takes L cycles per operation and can start T operations per cycle is full only when **L x T independent operations are in flight** (the same arithmetic as latency times bandwidth).

| Unit | L | T | Independent operations needed |
|---|---|---|---|
| FP add, Skylake | 4 | 2 | 8 |
| FP add, Golden Cove | 2 | 2 | 4 |
| FP add, Zen 4 | 3 | 2 | 6 |
| integer multiply | 3 | 1 | 3 |
| L1 load (about 4 to 5 cycles) | 4 to 5 | 2 | 8 to 10 |
| DRAM miss (about 100 ns) | 300 cycles | limited by MSHRs | 10 to 16 misses in flight (tutorial 11, section 8) |

This one line explains the accumulator trick of tutorial 11, section 10.1: to keep Skylake's two FP adders busy you need 8 independent sums.

### 4.5 Measured: the accumulator sweep

Sum 2,048 doubles (16 KB, so the data stays in the L1 cache) many times, with K independent accumulators (K = 1 is the plain loop; the others are the pattern of tutorial 11, section 10.1). Do the same with 64-bit multiplies. Cycles per element:

| K | FP add: predicted | FP add: *measured* | multiply: predicted | multiply: *measured* |
|---|---|---|---|---|
| 1 | 4 | 3.86 | 3 | 2.92 |
| 2 | 2 | 1.97 | 1.5 | 1.49 |
| 3 | 1.33 | 1.31 | 1 | 0.99 |
| 4 | 1 | 0.99 | 1 | 1.00 |
| 6 | 0.67 | 0.68 | 1 | 1.00 |
| 8 | 0.5 | 0.57 | 1 | 1.01 |
| 12 | 0.5 | 0.56 | 1 | 1.00 |
| 16 | 0.5 | 0.57 | 1 | 1.01 |

The *predicted* column is `max(L / K, 1 / T)`. Both curves behave exactly as the rule says. The adds fall until K = 8 (latency 4 x 2 ports) and then stop at about 0.5 (two ports). The multiplies fall until K = 3 and stop at 1.0, **because there is only one multiplier**: more accumulators cannot help. The first limit is a *latency* limit; the second is a *port* limit. Telling them apart is the skill of this tutorial.

*Note.* At the default `-O2`, GCC 13 packs independent accumulators into SIMD lanes by itself (the same result, since each sum is separate), and the times are almost the same. The flag `-fno-tree-vectorize` only keeps the picture scalar.

Division and square root are the slow units. They take many cycles and are not fully pipelined, so a loop full of divisions is limited by the divider. If the divisor is loop-invariant, multiplying by a precomputed reciprocal is faster where the rounding difference is acceptable (the compiler needs `-ffast-math` to do it for floats).

---

## 5. The load/store unit and the L1 data cache

**Address generation.** Loads and stores compute their address in an AGU, then use a load or store port. Skylake has two load ports and one store port. Lion Cove has 3 load and 3 store AGUs (up to 3 loads per cycle). The Cortex-X925 has 4 AGUs, 2 of them for stores. Apple's Firestorm is reported at 3 loads and 2 stores per cycle.

**Queues.** The load queue and store queue limit how many memory operations can be in flight: Skylake 72 loads and 56 stores; Apple's Firestorm about 150 and 106; Zen 5 a 104-entry store queue.

**The L1 data cache.** The first, smallest cache has a load-to-use latency of **3 to 5 cycles** on every core in section 7 (Lion Cove adds a second, larger cache at 9 cycles), and this has barely changed across designs. A pointer chase that always hits L1 costs 4 to 5 cycles per hop.

### 5.1 Store-to-load forwarding, measured

Tutorial 11 (section 7.2) said that forwarding fails when a load only partly overlaps a store. Here is the price. Three loops, each a chain through memory (store the value, load it back, add 1, repeat). The pointers are `volatile`, so every access really happens:

```c
*(volatile uint64_t *)&cell = v;  v = *(volatile uint64_t *)&cell + 1;  // same size
*(volatile uint64_t *)&cell = v;  v = *(volatile uint32_t *)&cell + 1;  // load inside the store
*(volatile uint32_t *)&cell = v;  v = *(volatile uint64_t *)&cell + 1;  // load bigger than the store
```

| Case | *Measured* cycles per round trip |
|---|---|
| 8-byte store, 8-byte load | 5.4 |
| 8-byte store, 4-byte load (inside the store) | 5.3 |
| 4-byte store, 8-byte load (partly outside) | **16.1** |

The first two forward from the store queue. In the third, the load needs bytes from the store *and* bytes from the cache, so it must wait until the store has been written into L1: about 11 cycles more. Reviewers report the same shape on other cores (Skylake about 5 against 15 cycles; on Zen 5, 7 cycles for a contained load and a 14-cycle penalty when forwarding fails).

**Rule for networking and embedded code:** read with the same width and alignment as you wrote. Parse a header with one load of the whole word and shift and mask in registers. Be careful with unions and bit-fields that are written in pieces and read as a whole.

---

## 6. Retire

Retire takes uops from the head of the ROB in order, at most **R** per cycle. R is at least W so that the core can catch up after a stall: Lion Cove renames 8 and retires 12 per cycle; Golden Cove renames 6 and retires 8. At retire, registers are freed, exceptions are taken (tutorial 11, section 6) and stores move to the store buffer to drain into L1. The ROB size is the window of tutorial 11, and appears in the table below.

---

## 7. Real cores side by side

Here are five cores: the Skylake core (the baseline, and the model used in this tutorial) and four recent big cores. Numbers are what the sources report; "n/p" means not published in the sources used.

| | Skylake-SP | AMD Zen 5 | Intel Lion Cove | Apple Firestorm (A14/M1) | Arm Cortex-X925 |
|---|---|---|---|---|---|
| Decode, instructions per cycle | 4 | 2 clusters x 4 | 8 | 8 | 10 |
| Micro-op cache | 1,536 entries | 6,000 entries | larger (up to 12 uops/cycle) | n/p | none |
| Rename / dispatch width | 4 | 8 | 8 | n/p | 10 |
| Retire width | n/p | n/p | 12 | n/p | n/p |
| ROB (the window) | 224 | 448 | 576 | about 630 | about 525 measured (Arm figure about 750 MOPs) |
| Integer ALUs | 4 | 6 | 6 | 6 (4 simple + 2 with multiply), plus a divider | 8 ALU ports and 3 branch units |
| FP / SIMD pipes | 2 for FP add and FMA | 4 vector pipes (512-bit on desktop) | 4 (256-bit in client parts) | 4 x 128-bit | 6 x 128-bit |
| Loads / stores per cycle | 2 / 1 | 4 AGUs (vector: 2 loads + 1 store) | 3 / 2 | 3 / 2 | 4 AGUs, 2 for stores |
| L1 data cache | 32 KB | 48 KB, 4 cycles | 48 KB at 4 cycles, then 192 KB at 9 cycles | 128 KB, 3 cycles | 64 KB, 4 cycles |
| L2 | 1 MB | 1 MB, 14 cycles | 2.5 to 3 MB, 17 cycles | shared by the cluster, 16 cycles | 2 to 3 MB, 12 cycles |

*Sources.* Skylake: [Chips and Cheese](https://chipsandcheese.com/p/skylake-intels-longest-serving-architecture) and the [IDF summary on TechPowerUp](https://www.techpowerup.com/215357/intel-skylake-microarchitecture-detailed) (224-entry ROB, 97-entry scheduler, 72 loads and 56 stores in flight). Zen 5: [Chips and Cheese on Strix Point](https://chipsandcheese.com/p/amds-strix-point-zen-5-hits-mobile), [Chips and Cheese on the Hot Chips talk](https://chipsandcheese.com/p/discussing-amds-zen-5-at-hot-chips-2024) and [WikiChip](https://en.wikichip.org/wiki/amd/microarchitectures/zen_5). Lion Cove: [Chips and Cheese](https://chipsandcheese.com/p/intels-lion-cove-architecture-preview) and [HWCooling](https://www.hwcooling.net/en/intels-new-p-core-lion-cove-is-the-biggest-change-since-nehalem/). Firestorm: [AnandTech](https://at-web1.www.anandtech.com/show/16226/apple-silicon-m1-a14-deep-dive/2) (Apple publishes no core details, and later Apple cores are reported to be wider still). Cortex-X925: [Chips and Cheese](https://chipsandcheese.com/p/arms-cortex-x925-reaching-desktop). The server relative, Neoverse V3, is also 10 wide with 4 x 128-bit SIMD pipes and a 64 KB L1 data cache ([review](https://jia.je/hardware/2026/06/13/arm-neoverse-v3/index.md), ROB estimated at about 384 MOPs). Some figures are vendor statements, some are measured by reviewers and some are estimates; sources disagree on the X925 ROB, so in an interview say "about".

**What the table says**

1. **Width and window grew a lot; latency did not.** Rename went from 4 to 10 and the ROB from 224 to about 600. The first-level cache is still 3 to 5 cycles, an FP add 2 to 4, a multiply 3. A loop limited by a dependence chain gains almost nothing from a new core; a loop with a lot of independent work gains a lot.
2. **Two ways to feed a wide front end.** Arm and Apple decode fixed-width instructions directly, 8 to 10 per cycle. x86 caches decoded uops (6,000 entries on Zen 5) and still decodes 8 per cycle on the miss path.
3. **Caches trade size for speed differently.** Apple: 128 KB at 3 cycles. Zen 5: 48 KB at 4. Lion Cove: a small 48 KB cache at 4 cycles in front of a 192 KB cache at 9. Tutorial 14 explains the trade-off.
4. **Wide is not free.** Doubling the ROB or the scheduler costs more than double in area and power (tutorial 11, section 9), so designers grow the structures that the target programs actually fill.

---

## 8. Reading instruction tables

When you need the latency or the port of an instruction, look it up; do not guess. Four sources:

- **[uops.info](https://uops.info):** latency, throughput, uop count and ports, measured on real Intel and AMD cores, one page per instruction form.
- **Agner Fog's instruction tables and microarchitecture manual:** free PDFs covering many x86 cores.
- **Arm Software Optimization Guides:** one per core (Cortex-X925, Neoverse V3 and so on) with latency, throughput and pipelines.
- **Intel and AMD optimisation manuals.**

**How to read a row.** From uops.info, `VADDPD (YMM, YMM, YMM)`:

| Core | Latency | Throughput | Uops | Ports |
|---|---|---|---|---|
| Skylake | 4 | 0.50 | 1 | `1*p01` |
| Golden Cove | 2 | 0.50 | 1 | `1*p15` |
| Zen 4 | 3 | 0.50 | 1 | `1*FP23` |

`1*p01` means one uop that may go to port 0 or port 1. A throughput of 0.50 is two per cycle, which matches two ports. `ADD r64, r64` shows `1*p0156`: any of four ports, throughput 0.25.

**Traps.**

- Latency can differ per operand pair (the address operand of a load is not the data operand).
- A form with a memory operand is the register form plus a load.
- Division, square root and denormal numbers have data-dependent latency.
- Tables are best cases: independent instructions, warm caches.
- A table for another core is not yours. Check the model.

**A tool that does the arithmetic.** `llvm-mca -mcpu=skylake-avx512 loop.s` predicts cycles per iteration and port pressure from the tables. For the loop of section 9.2 it printed a block throughput of 1.2 cycles against a measured 1.27 to 1.29. Use it as a first estimate: its model has no cache misses and only an approximate front end. Compiler Explorer shows the same analysis beside your C code.

---

## 9. Finding the bottleneck of a loop

### 9.1 The four bounds

For one iteration of a loop in steady state, compute:

| Bound | How to compute it |
|---|---|
| **Rename** | fused uops per iteration / W |
| **Ports** | for each kind of port: uops that need it / number of such ports (loads / 2, stores / 1, multiplies / 1, FP add and multiply / 2 on a Skylake core) |
| **Chain** | the latency of the longest loop-carried dependence chain, in cycles per iteration |
| **Memory** | where the data live, and how much per iteration (section 9.5) |

The time per iteration is about the **largest** of the four (a little more when two are close, because the scheduler is not perfect). The largest is the bottleneck. Fix it and the next one takes over.

### 9.2 Example 1: the front end, then the store port

```c
for (i = 0; i < n; i++) y[i] = x[i] + 1.0;
```

The compiler's loop (Intel syntax):

```text
movsd  xmm0, [rdi+rax]      ; load              1 uop
addsd  xmm0, xmm1           ; FP add            1 uop
movsd  [rsi+rax], xmm0      ; store             1 fused uop
add    rax, 8               ; index             1 uop
cmp    rax, rdx             ; jne loop          1 uop (macro-fused)
```

Bounds with W = 4: rename 5 / 4 = **1.25**; loads 1 / 2 = 0.5; FP add 1 / 2 = 0.5; store 1 / 1 = 1.0; no chain except the 1-cycle index. The bottleneck is the **front end (rename)**. *Measured:* 1.27 to 1.29 cycles per element, within 3% of the model. (A separate probe of a long, branch-free sequence sustained about 4.3 uops per cycle, so the VM's core is a little wider than 4 and part of this match is luck; what matters is the method, and the lesson "front-end limited, cured by unrolling", not the exact width.)

Unroll by 4, so that the index and the branch are paid once per 4 elements: (4 x 3 + 2) / 4 = 3.5 uops per element, so rename drops to 0.875. Now the **store port** (1.0) is the largest. *Measured:* 1.00 to 1.04. Unroll by 8: rename 0.81, store still 1.0, *measured* 1.02. No further gain.

Unrolling helped because loop overhead was the limit; it stopped helping when something else became the limit. To go below 1 cycle per element you need fewer stores per element, which means SIMD (tutorial 21).

### 9.3 Example 2: a latency chain, then a port

```c
for (i = 0; i < n; i++) h = h * M + p[i];       // M = 1000003, 64-bit unsigned
```

Per byte: `imul rax, rax, M`, `movzx edx, byte [rdi]`, `add rax, rdx`, plus the index and the fused branch: 5 uops. Bounds: rename 1.25; multiply 1 / 1 = 1.0; **chain: imul (3) + add (1) = 4 cycles per byte**. The bottleneck is the chain. *Measured:* 3.99 cycles per byte.

Restructure so that four bytes are handled per step:

```c
// M2 = M*M, M3 = M2*M, M4 = M3*M, computed once before the loop
for (i = 0; i + 4 <= n; i += 4)
    h = h * M4 + (p[i]*M3 + p[i+1]*M2 + p[i+2]*M + p[i+3]);
```

The result is **exactly the same**, because 64-bit integer arithmetic wraps around and wrapping arithmetic is associative (floating point is not). The bracket does not depend on `h`, so only `h * M4 + (...)` is on the chain: 4 cycles per 4 bytes, which is 1 per byte. But there are now four multiplies per four bytes and one multiplier, so the **multiply port** also gives 1.0 per byte. *Measured:* **1.01 cycles per byte**, four times faster. To go lower you must change the work itself: a different hash, SIMD, or a hardware CRC instruction.

### 9.4 Example 3: sum against dot product

Both loops want independent accumulators, but they need different numbers of them:

| Loop | Loads per element | Floor | Accumulators needed |
|---|---|---|---|
| `s += a[i]` | 1 (0.5 cycles) | 0.5 (loads and FP adds both 0.5) | 8 |
| `s += a[i] * b[i]` | 2 (1.0 cycles) | **1.0 (load ports)** | 4 |

*Measured dot product:* 3.96 cycles with 1 accumulator, 1.27 with 4 and 1.14 with 8. Four accumulators already reach the load limit on paper; the measured 1.27 is above 1.0 because the chain bound and the load bound are both exactly 1.0 and the scheduler is not perfect. Eight accumulators come closer. Beyond that, nothing helps: the loads are the limit.

### 9.5 Where the data live changes the bottleneck

The same sum, cycles per element, with the data in three places (*measured*):

| Data in | K = 1 | K = 2 | K = 4 | K = 8 | K = 8 against K = 1 |
|---|---|---|---|---|---|
| L1 cache (16 KB) | 3.86 | 1.97 | 0.99 | 0.57 | 6.8 times faster |
| L2 cache (512 KB) | 4.03 | 2.04 | 1.01 | 1.08 | 3.7 times |
| DRAM (512 MB) | 4.44 | 2.77 | 2.39 | 2.56 | 1.7 times |

In DRAM, 8 bytes per element at 2.4 cycles per element is about 10 to 11 GB/s, which is what one core of this machine could pull from memory. The adders are no longer the limit, so more accumulators do not help; prefetching, blocking (tutorial 14) or more cores (tutorial 18) would. **The same code change gives 6.8 times or 1.7 times depending on which bound was active.**

### 9.6 What to do about each bound

| Bottleneck | It means | What helps |
|---|---|---|
| **Rename** (front end) | too many uops for the width | fewer uops: unroll to cut loop overhead, SIMD, simpler addressing; for large code, PGO and layout |
| **Port** | too many uops of one kind | change the mix: shifts and adds for multiplies, SIMD, another instruction; or change the algorithm |
| **Chain** | a loop-carried dependence | break it: several accumulators, restructure (the hash), process independent items together |
| **Memory** | data not in a near cache | tutorials 13 to 16: layout, blocking, prefetching, parallel misses |

---

## 10. Top-down: from "slow" to "which of four"

### 10.1 The idea

Each cycle has W **slots**. Count in slots, and put every slot into exactly one of four buckets:

```text
every slot ─┬─ Retiring ......... a uop that completed and retired (useful work)
            ├─ Bad speculation .. a uop issued and then thrown away, or slots lost refilling
            ├─ Front-end bound .. no uop delivered although the back end could take one
            └─ Back-end bound ... the back end could not take a uop
                 ├─ Memory bound .. waiting for data (L1, L2, L3, DRAM, stores)
                 └─ Core bound .... waiting for a unit or a dependence (divider, ports, chains)
```

The four always add to 100%, so you cannot miss a cause. The second level splits each bucket: front-end into *latency* (instruction-cache or BTB misses) and *bandwidth* (decode, micro-op cache); bad speculation into branch mispredictions and machine clears; retiring into normal uops and microcode.

### 10.2 The arithmetic

For Skylake (W = 4). The event names change with each generation, and the tools compute this for you; the formulas show what the numbers mean:

- slots = 4 x cycles
- Front-end bound = uops not delivered (`IDQ_UOPS_NOT_DELIVERED.CORE`) / slots
- Retiring = retired slots (`UOPS_RETIRED.RETIRE_SLOTS`) / slots
- Bad speculation = (issued uops (`UOPS_ISSUED.ANY`) - retired slots + 4 x recovery cycles (`INT_MISC.RECOVERY_CYCLES`)) / slots
- Back-end bound = 100% - the other three

*Worked example* (the counters are made up to show the arithmetic, not measured). 1.0e9 cycles, so 4.0e9 slots; 0.4e9 uops not delivered; 1.6e9 issued; 1.2e9 retired; 0.1e9 recovery cycles.

```text
Front-end bound     0.4 / 4.0                       = 10%
Retiring            1.2 / 4.0                       = 30%
Bad speculation     (1.6 - 1.2 + 4 x 0.1) / 4.0     = 20%
Back-end bound      100 - 10 - 30 - 20              = 40%
```

The core retires 1.2 uops per cycle (30% of 4). The largest loss is the back end (40%), then bad speculation (20%). Go one level down inside the back end first: is it memory bound or core bound?

### 10.3 Tools

| Vendor | Command | Notes |
|---|---|---|
| Intel | `perf stat --topdown`, or `perf stat -M TopdownL1` | four percentages; older cores need system-wide mode (`-a`) for `--topdown` |
| AMD Zen 4 and Zen 5 | `perf stat -M PipelineL1` | `frontend_bound`, `bad_speculation`, `backend_bound`, `retiring`, `smt_contention`; level 2 is `PipelineL2` |
| Arm Neoverse | Arm's `topdown-tool` | groups `Topdown_L1`, then `Topdown_Frontend` or `Topdown_Backend`; slots = width x cycles |

Names vary with the kernel and tool version; `perf list metricgroup` shows what yours supports. All of them need hardware counters, which most cloud VMs hide: use a bare-metal instance or your own machine.

**The method.** Level 1: find the largest bucket that is not Retiring. Level 2: look inside it. Change one thing. Measure again.

### 10.4 Predict first, then measure

The value is in being wrong on paper. Predictions from this tutorial's own numbers (not measurements):

| Program | Expected | Why |
|---|---|---|
| sum, 1 accumulator, in L1 | back-end bound (core), **about 19% retiring** | 3 fused uops per element in 3.86 cycles: 3 / (4 x 3.86) = 19%. The rest of the slots wait for the add chain |
| sum, 8 accumulators, in L1 | **about 55% retiring** | 10 uops per 8 elements in 8 x 0.568 = 4.5 cycles: 10 / (4 x 4.5) = 55% |
| sum, 8 accumulators, 512 MB | back-end bound (memory) | the adders idle, waiting for data |
| pointer chase | back-end bound (memory), retiring below 15% | one miss at a time (tutorial 11) |
| branchy loop on random data | bad speculation | tutorial 10 |
| huge flat code base (a big server) | front-end bound | micro-op cache and instruction-cache misses |

**IPC alone is not a diagnosis.** A thread spinning on a lock shows a high retiring share and does nothing useful. Top-down tells you *why* slots are lost; it cannot tell you whether the retired work was worth doing.

---

## 11. What this means for your code

- **Name the bound before changing the code.** Rename, port, chain or memory each have a different fix.
- **Count by hand, then check.** Count fused uops, ports and the chain for the hot loop, look at the disassembly, and compare with `llvm-mca` and a measurement.
- **Latency does not improve with new cores; independent work does.** Structure algorithms and data so that independent items sit side by side (batches, several streams, SoA layouts).
- **Unroll for the slowest latency you target.** Eight accumulators are harmless on a core with 2-cycle adds (it just runs at the port limit); four accumulators lose a factor of 2 on Skylake.
- **Do not read the profile of a virtual machine as truth.** Counters may be missing or virtualised.
- **The memory row is usually the biggest.** Fix layout and locality (tutorials 13 and 14) before squeezing ports.

---

## 12. Mistakes that cost points

1. **"A wider core runs my code faster."** Only if the code offers independent work. A loop limited by a dependence chain is unchanged by width (section 7).
2. **Counting instructions instead of fused uops.** Rename counts fused uops; ports count the unfused parts. Fusion of `cmp`/`jne` and of load-and-op changes the count.
3. **Mixing up latency and throughput.** A dependent chain pays the latency of each step; independent copies pay the reciprocal throughput. The same instruction can be limited by either.
4. **Unrolling without checking the port.** More accumulators fix a *latency* limit. They do nothing about a *port* limit (the multiply stays at 1 per cycle).
5. **Fixing the wrong bound.** Eight accumulators on a DRAM-bound sum give 1.7 times, not 7 (section 9.5).
6. **Assuming every x86 core has the same latencies.** FP add is 4, 2 or 3 cycles on Skylake, Golden Cove and Zen 4. Code tuned on one core can be 1.5 to 2 times slower on another.
7. **Writing with one width and reading with another.** A failed store-to-load forward costs about 11 extra cycles (5.4 against 16.1 measured).
8. **Treating IPC as the goal.** Spinning on a lock retires happily. Top-down says where slots go, not whether the work was needed.
9. **Mixing "stated", "measured" and "estimated" numbers.** Core sizes come from vendors, reviewers' microbenchmarks and guesses; they can disagree (the X925 ROB). Say "about".
10. **Reading a top-down profile from a virtual machine without checking the counters exist.**

---

## 13. Questions you will be asked

**Walk me through the stages of a modern out-of-order core.** The predictor runs ahead and fetch follows. Decode (or the micro-op cache) makes uops. Rename maps registers and allocates a ROB entry, a scheduler entry and a queue entry. The scheduler sends ready uops to ports. Execution units run them; loads and stores go through the load/store unit and the L1 cache. Retire commits in order, frees resources and lets stores drain.

**What limits IPC?** The narrowest stage (usually rename, W), the ports, the dependence chains in the code, branch mispredictions, the front end (code size), and memory. Which one is active depends on the loop.

**What is the difference between latency and throughput of an instruction?** Latency is cycles from inputs ready to result ready. Reciprocal throughput is cycles between the starts of independent copies. A chain pays latency; independent work pays throughput.

**How many accumulators do you need to sum doubles at full speed when FP add has latency 4 and there are two FP ports?** Eight: latency times throughput, 4 x 2. On a core with 2-cycle adds, four.

**Why does a wider core not always help?** The speed is the maximum of four bounds: rename, ports, dependence chain and memory. Width only changes the first. A loop limited by a chain, a single multiplier or DRAM gains nothing.

**What is a micro-op cache, and why do x86 cores have one?** A cache of already-decoded uops. x86 instructions are variable length and hard to decode in parallel, so a hit saves decode time and power. Fixed-width Arm instructions are cheap to decode, and cores such as the Cortex-X925 have no such cache.

**What is macro-fusion?** A compare or test followed by a conditional jump is renamed and executed as one uop, which saves rename width.

**How do you find the bottleneck of a loop?** Count fused uops (rename bound), count uops per port group (port bound), find the longest loop-carried chain (chain bound) and decide where the data live (memory bound). The largest is the bottleneck. Then confirm with a measurement or a tool such as `llvm-mca`.

**What is the top-down method?** Count slots (width times cycles) and put each in one of four buckets: retiring, bad speculation, front-end bound, back-end bound. Start at level 1, go one level down inside the largest loss, change one thing, measure again.

**A loop has an IPC of 0.4. What do you do?** Do not guess: run top-down. If it is back-end memory bound, look at the cache level and the access pattern; if bad speculation, look at branches; if front-end bound, look at code size and layout.

**When does store-to-load forwarding fail, and what does it cost?** When the load is not fully contained in one older store (for example a 4-byte store then an 8-byte load). The load waits until the store reaches L1: about 11 extra cycles on the cloud VM (16 against 5).

**How do you read a line in an instruction table?** Latency (per operand pair), reciprocal throughput, uop count, and the ports. `1*p01` is one uop on port 0 or 1; a throughput of 0.5 matches two ports.

**How do Arm and x86 big cores differ in the front end?** Fixed 4-byte instructions allow 8 to 10 decoders without a uop cache; x86 needs length decoding and caches decoded uops, and still decodes 4 to 8 per cycle on a miss.

**Why do cloud profiles sometimes show no top-down data?** Virtual machines often do not expose the hardware performance counters.

---

## 14. Problems

**How to use this section.** Try each problem before you read the answers. All four are paper problems (25 to 30 minutes each); B has an optional short program.

**Model core M** (used in A and B; it behaves like a Skylake core): rename and retire 4 fused uops per cycle; 4 integer ALU ports, only one of which can multiply (latency 3); 2 FP add/multiply ports (latency 4); 2 loads per cycle; 1 store per cycle; a compare fused with its jump is one uop; a load folded into an arithmetic instruction is one fused uop and one load. All data are in L1. Clock 3 GHz.

### Problem A (paper): the four bounds of a loop

Loop 1 (a scaling loop, `y[i] = 2.0 * x[i] + 1.0`):

```text
movsd  xmm0, [rdi+rax]      ; load
mulsd  xmm0, xmm2           ; FP multiply
addsd  xmm0, xmm1           ; FP add
movsd  [rsi+rax], xmm0      ; store
add    rax, 8
cmp    rax, rdx             ; fused with the jump below
jne    loop
```

(a) Compute the rename, port and chain bounds for one element. What is the bottleneck, and how many cycles per element?
(b) Unroll by 4 and by 8 (one `add` and one `cmp/jne` per unrolled body). Recompute. Which limit takes over, and what would you do to go lower?
(c) Loop 2 is `y[i] = x[i] / d` with a divider that can start one divide every 4 cycles. Which bound decides? Rewrite it to avoid the divide, give the new cycles per element, and name the catch.
(d) For loop 1 (rolled), loop 2 and the hash loop of section 9.3, say whether a core with W = 8 (everything else equal) would be faster.

*Where:* paper. *How:* section 9. Count fused uops first, then ports, then the chain.

### Problem B (paper, optional short C): reductions

(a) On core M at 3 GHz, with n = 1,000,000 doubles read from L1 (assume repeated passes), compute cycles per element and the time in ms for `s += a[i]` with K = 1, 2, 4, 8 and 16 accumulators.
(b) Do the same for the dot product `s += a[i] * b[i]` (the multiply is not on the chain). Where does it stop, and why there? How many accumulators does each loop need?
(c) Three cores all have two FP add ports, with add latencies 2 (core G), 3 (core Z) and 4 (core M). How many accumulators does the sum need on each? A library written with 4 accumulators (tuned on G) runs on all three: cycles per element on each, and the loss against the best.
(d) The sum over 512 MB (64 million doubles) in DRAM on core M, at a memory bandwidth of 10 GB/s: cycles per element at the bandwidth limit, and the best speed-up that 8 accumulators can give over 1.
(e) *(Optional.)* Write `sum1`, `sum2`, `sum4` and `sum8` (the pattern of tutorial 11, section 10.1). Compile with `gcc -O2 -fno-tree-vectorize`, run each 100,000 times over 2,048 doubles, and compute cycles per element as nanoseconds per element times your clock in GHz. Compare with section 4.5.

*Where:* paper for (a) to (d); your machine for (e). *How:* sections 4.4, 4.5 and 9.4 to 9.5.

### Problem C (paper): read a top-down breakdown

Three programs on a 4-wide Skylake-style core. All run 1.0e9 cycles.

| | Uops not delivered | Uops issued | Retired slots | Recovery cycles |
|---|---|---|---|---|
| P1 | 0.10e9 | 3.40e9 | 3.20e9 | 0.02e9 |
| P2 | 0.40e9 | 2.00e9 | 1.20e9 | 0.30e9 |
| P3 | 0.05e9 | 0.45e9 | 0.44e9 | 0.001e9 |

(a) Compute the four level-1 buckets for each program, and the uops per cycle.
(b) For each program, name the largest loss and the first thing you would look at.
(c) Match them to: the `y[i] = x[i] + 1.0` loop unrolled by 4 (section 9.2), a branchy loop on random data (tutorial 10), a pointer chase (tutorial 11).
(d) A fourth program shows front-end bound 45% and retiring 40%. What do you check at level 2, and give two likely causes with fixes.
(e) Why might you not be able to collect these numbers on a cloud instance?

*Where:* paper. *How:* section 10. Slots = 4 x cycles.

### Problem D (paper): tables, cores and windows

(a) A loop does `acc = _mm256_add_pd(acc, v[i])` (a `vaddpd`) with one accumulator. Using the table of section 4.3, how many cycles per vector on Skylake, Golden Cove and Zen 4, and how many accumulators does each core need to run at its limit?
(b) A library uses 4 accumulators because that was best on Golden Cove. Cycles per vector on each of the three cores, and the loss against each core's best.
(c) A packet parser at 10 million packets per second per 3 GHz core writes a 16-bit length field and then reads the 32-bit word that contains it. Use the 16.1 and 5.4 cycles of section 5.1. How many extra cycles per packet, what share of the 300-cycle budget is that, why might the real cost be lower, and what is the fix?
(d) A loop has one independent DRAM miss (250 cycles) every 60 instructions; the MSHRs are not the limit. Using the ROB sizes of section 7, what is the memory-level parallelism and the cost per miss on Skylake (224), Zen 5 (448) and Lion Cove (576)? Why does a bigger ROB not help the loop of (a)?

*Where:* paper. *How:* sections 4.3, 5.1, 7 and tutorial 11, section 8.

---

## Answers

### Problem A

(a) Fused uops: load 1, multiply 1, add 1, store 1, `add` 1, `cmp/jne` 1 = **6**. Rename 6 / 4 = **1.5**. Loads 1 / 2 = 0.5. FP (multiply and add) 2 / 2 = 1.0. Store 1 / 1 = 1.0. There is no chain except the 1-cycle index. The bottleneck is **rename: 1.5 cycles per element** (measured on the VM: 1.53).

(b) Unroll by 4: (4 x 4 + 2) / 4 = 4.5 uops per element, so rename = **1.125**; FP and store are 1.0, so rename is still the largest (measured 1.22). Unroll by 8: 4.25 uops, rename **1.06**: almost tied with the FP ports and the store port at 1.0. Nothing more can be gained from unrolling. To go lower you must do more work per uop: SIMD (4 doubles per multiply, add and store), tutorial 21.

(c) The divider gives 4 cycles per element; the other bounds are about 1.25. The **divider decides: 4 cycles**. Compute `r = 1.0 / d` once and use `y[i] = x[i] * r`: the loop is then like section 9.2 (rename 1.25 rolled, store 1.0 unrolled), so about **1.25** cycles, 3.2 times faster (4 times when unrolled). The catch: `x * (1/d)` can differ from `x / d` in the last bit, so the compiler does it by itself only with `-ffast-math` (or `-freciprocal-math`).

(d) With W = 8: loop 1 rolled has rename 0.75, so the bottleneck becomes FP/store at 1.0: **faster, 1.5 to 1.0**. Loop 2 is divider-bound at 4: **no change**. The hash loop is chain-bound at 4 cycles per byte: **no change**. Width helps only when rename is the largest bound.

### Problem B

(a) Sum, loads 0.5 and FP add 0.5 as floors, chain 4 / K:

| K | cycles per element | time for n = 10^6 at 3 GHz |
|---|---|---|
| 1 | 4 | 1.33 ms |
| 2 | 2 | 0.67 ms |
| 4 | 1 | 0.33 ms |
| 8 | 0.5 | 0.17 ms |
| 16 | 0.5 | 0.17 ms |

(b) Dot product: two loads per element, so the load ports give a floor of **1.0**: K = 1: 4 (1.33 ms); K = 2: 2 (0.67 ms); K = 4: 1 (0.33 ms); K = 8 and 16: still 1 (0.33 ms). The dot product stops at 1.0 because of the **load ports**, so it needs only **4** accumulators; the sum needs **8** (its floor is 0.5).

(c) Accumulators needed = latency x 2 ports: G **4**, Z **6**, M **8**. With 4 accumulators the chain bound is latency / 4: G 0.5 (best, no loss); Z 0.75 (1.5 times the best 0.5); M 1.0 (2 times the best). Unroll for the slowest latency you target.

(d) 512 MB at 10 GB/s takes 51 ms for 64 million elements: 0.8 ns, or **2.4 cycles per element** at 3 GHz. One accumulator is compute-bound at 4 cycles, so the best speed-up is 4 / 2.4 = **1.67**. We measured 1.7 (4.44 against 2.56 cycles).

(e) Expect about 3.9, 2.0, 1.0 and 0.5 to 0.6 cycles per element on a Skylake-class core (we measured 3.86, 1.97, 0.99 and 0.57); other cores differ by their latency and port count. If you see no change at all, the compiler may have vectorised or reassociated; check the flags.

### Problem C

(a) Slots = 4.0e9 for each.

| | Front-end | Retiring | Bad speculation | Back-end | Uops per cycle |
|---|---|---|---|---|---|
| P1 | 2.5% | 80% | 7% | 10.5% | 3.2 |
| P2 | 10% | 30% | 50% | 10% | 1.2 |
| P3 | 1.25% | 11% | 0.35% | 87.4% | 0.44 |

For P2, bad speculation = (2.0 - 1.2 + 4 x 0.3) / 4.0 = 2.0 / 4.0 = 50%. For P3, back-end = 100 - 1.25 - 11 - 0.35 = 87.4%.

(b) **P1:** mostly useful work (80% retiring). The 10.5% back-end loss is small; to go faster, do more work per uop (SIMD) rather than hunt stalls. **P2:** bad speculation is half the slots: look at the branch mispredict rate and which branches (level 2: mispredicts or machine clears), then tutorial 10 (sort, branchless, hints, PGO). **P3:** back-end bound at 87%: go to the memory-bound level and find which cache level, then change layout or create memory-level parallelism (tutorials 11, 13 and 14).

(c) P1 is the unrolled `y = x + 1` loop (3.5 uops per cycle is 87%, in the same range); P2 is the branchy loop on random data; P3 is the pointer chase.

(d) Look at **front-end latency against bandwidth**. Latency (instruction-cache, BTB, micro-op-cache misses, re-steers) points to a large code footprint: use PGO and function ordering, keep cold paths out of the hot path, avoid scattered indirect calls. Bandwidth (decode, microcode) points to microcode instructions or very long instruction sequences: replace the instruction, restructure the loop.

(e) Cloud virtual machines often do not expose the hardware performance counters, so `perf` cannot read the slot events. Use a bare-metal instance or your own machine (our VM had none).

### Problem D

(a) One accumulator: the cycles per vector equal the latency: Skylake **4**, Golden Cove **2**, Zen 4 **3**. Accumulators needed = latency / 0.5: **8**, **4** and **6**.

(b) With 4 accumulators: cycles per vector = max(0.5, latency / 4). Golden Cove: max(0.5, 0.5) = **0.5** (best, no loss). Zen 4: 0.75, so **1.5 times** the best. Skylake: 1.0, so **2 times** the best.

(c) Extra cost = 16.1 - 5.4 = **10.7 cycles per packet**. The budget is 3 GHz / 10 Mpps = 300 cycles, so that is **3.6%**. The real cost can be lower: only the instructions that depend on the load wait, and the window keeps running independent work (including the next packet), so the full cost applies only when the load is on the critical path. Fix: write and read with the same width; or load the whole 32-bit word once and extract the field with a shift and a mask in registers.

(d) MLP = ROB / 60, rounded down: Skylake **3**, Zen 5 **7**, Lion Cove **9**. Cost per miss = 250 / MLP: **83**, **36** and **28** cycles. More instructions in the window means more misses overlap. The loop of (a) is one serial chain; a bigger window cannot shorten a chain, because each add needs the previous result (the window helps only when *different* chains or misses are available to overlap).

---

## Cheat sheet

- **Core:** predictor runs ahead, fetch, decode or micro-op cache, rename and allocate (W per cycle), scheduler, ports and units, load/store unit and L1, retire in order (R at least W). The speed is that of the narrowest stage.
- **Uops:** macro-fusion (compare and jump = 1), micro-fusion (load and op = 1 up to the scheduler). Rename counts **fused** uops; ports count unfused parts.
- **Front end:** Arm decodes 8 to 10 fixed-width instructions; x86 needs length decoding, so it adds a micro-op cache and still decodes 4 to 8. Starved by instruction-cache and BTB misses, large code, microcode instructions, many taken branches.
- **Rename:** allocates ROB, scheduler, register, load/store entries; `xor eax, eax` and register moves are free there.
- **Ports (Skylake):** p0 and p1 ALU, FP and vector (p1 also the only integer multiplier); p5, p6 ALU (p6 branches); p2 and p3 loads; p4 store data; p7 store address. 2 loads and 1 store per cycle.
- **Latency and throughput:** a chain pays latency, independent work pays 1 / throughput. Independent operations needed = latency x throughput (FP add 4 x 2 = 8; multiply 3 x 1 = 3).
- **Four bounds:** rename = fused uops / W; ports = uses / ports; chain = longest loop-carried latency; memory = where the data live. Time is about the largest.
- **Fixes:** rename: fewer uops (unroll, SIMD, PGO); port: change the mix or algorithm; chain: break it (accumulators, restructure); memory: layout, blocking, prefetch.
- **Measured (cloud VM, 3.3 GHz):** FP-add chain 3.86 cycles/element, 8 accumulators 0.57; multiply flat at 1.0 from K = 3; `y = x + 1` 1.27 rolled and 1.00 unrolled (store port); hash 3.99 then 1.01 cycles/byte; dot product floor 1.0 (loads); forwarding 5.4 cycles, partial-overlap failure 16.1; sum speed-up of 8 accumulators: 6.8 in L1, 3.7 in L2, 1.7 in DRAM.
- **Cores:** width 4 to 10, ROB 224 to about 630; L1 3 to 5 cycles; FP add 2 to 4. Width and window grew; latency did not.
- **Tables:** uops.info, Agner Fog, Arm optimisation guides; read latency, throughput, uops, ports; `llvm-mca` for estimates.
- **Top-down:** slots = W x cycles; Retiring, Bad speculation, Front-end bound, Back-end bound (memory or core). Intel `perf stat --topdown`, AMD `-M PipelineL1`, Arm `topdown-tool`. Needs hardware counters. IPC alone is not a diagnosis.

**Next:** tutorial 13, Cache Basics (locality, the memory hierarchy, lines, direct-mapped and set-associative caches, tag, index and offset arithmetic, replacement, write policies, AMAT). Part 3 begins here: the memory system. Say "next" when you are ready.
