# CO 09: Pipelining and Hazards

*Part of the [Computer Organization plan](00_plan.md) · Core tutorial · Time: about 3 hours (1.5 reading, 1.5 problems) · You need: [tutorial 08](08_datapath_and_control_unit.md) (the datapath and its control signals), [tutorial 04](04_sequential_logic_and_state_machines.md) (registers, setup time, clock period) and [tutorial 06](06_isa_design.md) (RISC-V instructions)*

By the end of this tutorial you can:

- draw a pipeline diagram for a sequence of instructions, including stalls and flushes;
- compute the pipelined clock, the latency, the throughput and the speedup;
- name the three kinds of hazard and say how each one is avoided or reduced;
- explain forwarding (which values go where) and why a load followed by a use still costs one cycle;
- compute CPI with stalls and compare two designs with it;
- explain how a pipeline takes an exception or an interrupt without breaking the program;
- say what long operations (multiply, divide) do to a pipeline, and why a deeper pipeline stops paying off.

The pipeline here is the classic one: **in order**, **one instruction per cycle**, **five stages**. Real cores are far more complex (tutorials 11 and 12), but every idea they use is already in this one. All the cycle counts and diagrams below were checked with a small timing model of the pipeline, and every count matched.

---

## 1. The idea

In the single-cycle design of tutorial 08, one instruction used the whole datapath for a whole clock period. While the ALU worked, the instruction memory sat idle. **Pipelining** puts that idle hardware to work: split the datapath into stages, and let a different instruction occupy each stage in the same cycle. Think of an assembly line: a car takes the same time to build, but a finished car leaves the line every few minutes.

The classic RISC pipeline has five stages. The delays are the ones from tutorial 08.

| Stage | Name | Work | Delay |
|---|---|---|---|
| 1 | **IF** (instruction fetch) | read the instruction at PC; compute PC + 4 | 200 ps |
| 2 | **ID** (decode and register read) | decode, read `rs1` and `rs2`, build the immediate, make the control signals | 100 ps |
| 3 | **EX** (execute) | ALU operation, address calculation, branch comparison | 200 ps |
| 4 | **MEM** (memory) | load or store in the data memory | 200 ps |
| 5 | **WB** (write back) | write the result into `rd` | 100 ps |

A pipeline diagram has one row per instruction and one column per clock cycle. Here are five instructions that do not depend on each other:

```text
cycle                     1    2    3    4    5    6    7    8    9
add x1, x2, x3           IF   ID   EX  MEM   WB
sub x4, x5, x6                IF   ID   EX  MEM   WB
and x7, x8, x9                     IF   ID   EX  MEM   WB
or x10, x11, x12                        IF   ID   EX  MEM   WB
xor x13, x14, x15                            IF   ID   EX  MEM   WB
```

Look at cycle 5: all five stages are busy, each with a different instruction. From then on **one instruction finishes every cycle**, so the ideal CPI is 1. The first instruction still took five cycles; the gain is in how often instructions *finish*.

> **Latency versus throughput.** Pipelining does **not** make one instruction faster (it makes it a little slower, section 3). It raises **throughput**: instructions completed per second.

Why RISC suits this. Every instruction is 32 bits, so fetch is one simple step. The formats are regular, so decode is quick. Only loads and stores touch memory, and always in the same stage. Tutorial 06 listed these as ISA design goals; here you see the payoff. x86 gets the same effect by turning its instructions into simple micro-ops first (tutorial 08, section 8, and tutorial 12).

---

## 2. The pipelined datapath

To overlap instructions, put a register between every pair of stages. These **pipeline registers** hold everything the next stage needs, because in the next cycle the previous stage already works on the *next* instruction.

```text
         IF                  ID                  EX                 MEM                 WB
  +---------------+   +---------------+   +---------------+   +--------------+   +---------------+
  | PC            |   | register file |   | ALU           |   | data memory  |   | write back    |
  | instr memory  |==>| control       |==>| branch adder  |==>|              |==>|               |--> register file
  +---------------+   +---------------+   +---------------+   +--------------+   +---------------+
                  IF/ID               ID/EX              EX/MEM             MEM/WB      (pipeline registers)
```

What each pipeline register carries:

| Register | Holds |
|---|---|
| **IF/ID** | the instruction, PC + 4 |
| **ID/EX** | the control signals, values of `rs1` and `rs2`, the immediate, the register numbers `rs1`, `rs2`, `rd` |
| **EX/MEM** | the remaining control signals, the ALU result, the store data (`rs2`), `rd`, the branch decision and target |
| **MEM/WB** | the last control signals (`RegWrite`, `MemtoReg`), the ALU result, the data read from memory, `rd` |

Two details that matter:

- **Control signals travel with the instruction.** They are made once, in ID, and handed from register to register. `ALUSrc` and `ALUOp` are used in EX; `MemRead`, `MemWrite` and `Branch` in MEM; `RegWrite` and `MemtoReg` in WB (the signals of tutorial 08, split by *when* they are needed).
- **`rd` travels too.** By the time an instruction reaches WB, the instruction in ID is a different one. The write must use the `rd` that was carried along, not the one currently being decoded.

**The register file works in two halves.** It writes in the first half of a cycle and is read in the second half, so an instruction in WB and a later instruction in ID can use the same register in the same cycle and the reader sees the new value. (Equivalently, the register file can bypass the value internally.) The diagrams below assume this.

---

## 3. How much faster?

The clock period must fit the **slowest stage**, plus the overhead of the pipeline register itself (its clock-to-Q delay and setup time, tutorial 04). Say the overhead is 20 ps:

```text
clock T  = slowest stage + overhead = 200 + 20 = 220 ps          (single-cycle was 800 ps)
latency  = 5 stages x 220 ps = 1100 ps                           (single-cycle: 800 ps; pipelining made it LONGER)
throughput = 1 instruction per 220 ps                            (single-cycle: 1 per 800 ps)
```

For N instructions with no stalls, the first one needs 5 cycles to come out and every other instruction one more cycle:

```text
time = (5 + N - 1) x T
```

| N | Single-cycle (800 ps each) | Pipelined | Speedup |
|---|---|---|---|
| 1 | 800 ps | 1,100 ps | 0.73 (slower) |
| 5 | 4,000 ps | 1,980 ps | 2.02 |
| 100 | 80,000 ps | 22,880 ps | 3.50 |
| 1,000 | 800,000 ps | 220,880 ps | 3.62 |
| very large | | | 800 / 220 = 3.64 |

So five stages gave 3.6 times, not 5. There are three reasons, and they apply to every real pipeline:

1. **The stages are unbalanced.** The five jobs take 200, 100, 200, 200 and 100 ps. The slowest sets the clock, so ID and WB waste half their cycle. With these delays even a perfect pipeline could not beat 800 / 200 = 4.
2. **Pipeline registers add overhead** to every stage (here 20 ps, which turned 4.0 into 3.6).
3. **Hazards** (the rest of this tutorial) make instructions wait.

To go faster, designers balance the stages, and split slow stages into two (section 11 shows where that stops paying).

---

## 4. Hazards

A **hazard** is a situation where the next instruction cannot run in its normal cycle. The pipeline handles it by delaying instructions.

| Word | Meaning |
|---|---|
| **stall** | an instruction waits in its stage; everything behind it waits too |
| **bubble** | an empty slot that travels down the pipeline (a stalled instruction leaves one behind it); it does nothing and writes nothing |
| **flush** | throwing away instructions that were fetched by mistake (they become bubbles) |

There are three kinds:

| Kind | The problem | Main cures |
|---|---|---|
| **Structural** | two instructions need the same hardware in the same cycle | more hardware, or a stall |
| **Data** | an instruction needs a value that an earlier instruction has not produced yet | forwarding, a stall, better instruction order |
| **Control** | the next instruction to fetch is not known yet (a branch or jump) | prediction, resolving earlier, flushing |

In the diagrams below a **starred** stage name (`ID*`, `IF*`) means the instruction is stuck in that stage for this cycle. The last, unstarred `ID` is the cycle in which it finally moves on.

---

## 5. Structural hazards

Three common ones:

1. **One memory for instructions and data.** In the cycle where a `lw` is in MEM, the instruction three places behind it is in IF. If both use one memory, they collide. The cure is two memories, which is what **separate instruction and data caches** are (tutorial 13). This is also the reason tutorial 08's single-cycle datapath needed two memories.
2. **The register file.** In one cycle the pipeline reads two registers (ID) and writes one (WB), so it needs two read ports and one write port.
3. **A unit that is not pipelined**, such as a divider that is busy for many cycles. A second divide has to wait (section 10).

A structural hazard costs a stall for every collision. The cure is always "duplicate or pipeline the hardware", so the decision is cost against how often the collision happens. A divider is rare enough that making it a stall is usually accepted.

---

## 6. Data hazards

### 6.1 The problem (RAW)

```text
add x1, x2, x3     # writes x1 in cycle 5 (WB)
sub x4, x1, x5     # reads x1 in its ID stage, long before that
```

This is a **RAW** hazard (read after write): `sub` needs the value that `add` has not written yet. Here is the pipeline if there is *no* help at all, so each reader must wait until the value is in the register file (the register file writes in the first half and reads in the second half of cycle 5):

```text
cycle                     1    2    3    4    5    6    7    8    9   10   11
add x1, x2, x3           IF   ID   EX  MEM   WB
sub x4, x1, x5                IF  ID*  ID*   ID   EX  MEM   WB
and x6, x1, x7                     IF  IF*  IF*   ID   EX  MEM   WB
or x8, x1, x9                                     IF   ID   EX  MEM   WB
xor x10, x1, x11                                       IF   ID   EX  MEM   WB
```

`sub` waits **2 cycles** in ID. Everything behind it waits as well, but `and` has no extra delay of its own: once `sub` has moved on, x1 is already in the register file.

### 6.2 Forwarding (bypassing)

The value that `sub` needs exists much earlier than cycle 5. The ALU computed it at the end of cycle 3. **Forwarding** adds wires and two multiplexers that feed the ALU's inputs from the later pipeline registers instead of waiting for the register file:

```text
cycle                     1    2    3    4    5    6    7    8    9
add x1, x2, x3           IF   ID   EX  MEM   WB
sub x4, x1, x5                IF   ID   EX  MEM   WB
and x6, x1, x7                     IF   ID   EX  MEM   WB
or x8, x1, x9                           IF   ID   EX  MEM   WB
xor x10, x1, x11                             IF   ID   EX  MEM   WB
```

- In cycle 4, `sub` takes `x1` from the **EX/MEM** register (the ALU result of the instruction just ahead of it).
- In cycle 5, `and` takes `x1` from the **MEM/WB** register (the value of the instruction two ahead, being written back).
- `or`, three behind, reads the register file normally (it is in ID in cycle 5).

With forwarding, back-to-back ALU instructions have **no stall at all**. Total: 9 cycles instead of 11.

The forwarding unit is a few comparators. For the first ALU input (the other one is the same with `rs2`):

```text
if EX/MEM.RegWrite and EX/MEM.rd != x0 and EX/MEM.rd == ID/EX.rs1:
        ALU input A = EX/MEM.ALUresult            # from the instruction 1 ahead
else if MEM/WB.RegWrite and MEM/WB.rd != x0 and MEM/WB.rd == ID/EX.rs1:
        ALU input A = MEM/WB value                # from the instruction 2 ahead (ALU result or loaded data)
else:
        ALU input A = value read in ID
```

Two traps hide in those lines:

- **The newest value wins.** In `add x1,x1,x2` / `add x1,x1,x3` / `add x1,x1,x4`, the third add has x1 available both in EX/MEM (from the second add) and in MEM/WB (from the first). The first condition is checked first because the closest instruction has the newest value.
- **`x0` is never forwarded.** In RISC-V `x0` always reads as zero. `addi x0, x1, 5` writes nothing, so a following `add x2, x0, x3` must still see 0. That is what `rd != x0` is for. (This is a classic bug in student designs.)

### 6.3 The load-use hazard: forwarding cannot fix everything

```text
lw  x1, 0(x2)      # the value arrives from the data memory at the END of MEM (cycle 4)
add x4, x1, x5     # needs x1 at the START of its EX stage
```

The ALU result of the instruction ahead is available at the end of EX. A loaded value is available only at the end of **MEM**, one stage later. A forwarding wire cannot send a value backwards in time, so one stall is unavoidable:

```text
cycle                     1    2    3    4    5    6    7    8
lw x1, 0(x2)             IF   ID   EX  MEM   WB
add x4, x1, x5                IF  ID*   ID   EX  MEM   WB
sw x4, 0(x6)                       IF  IF*   ID   EX  MEM   WB
```

The **hazard detection unit** (in ID) catches this case:

```text
if ID/EX.MemRead and (ID/EX.rd == IF/ID.rs1 or ID/EX.rd == IF/ID.rs2):
        stall: keep the PC and IF/ID unchanged for one cycle, and put a bubble (all control signals 0) into ID/EX
```

After the stall, the value is forwarded from MEM/WB. Notice that a value loaded and then **stored** (`lw x1,...` followed by `sw x1,...`) needs no stall: the store needs its data only in MEM, so it is forwarded there.

### 6.4 WAR and WAW: not a problem here

Two other kinds of dependence exist:

| Name | Meaning | Example |
|---|---|---|
| **WAR** (write after read) | a later instruction writes a register that an earlier one still has to read | `sub x4, x1, x5` then `add x1, x2, x3` |
| **WAW** (write after write) | two instructions write the same register; the later must win | `add x1, ...` then `sub x1, ...` |

In this pipeline neither can go wrong. Registers are *read* early (ID) and *written* late (WB), and instructions go through in order, so a later instruction cannot write before an earlier one has read, and two writes always happen in program order. They matter in two places: when instructions can finish **out of order** (tutorial 11 removes them by renaming registers) and when some operations take longer than others (section 10).

### 6.5 The compiler helps: instruction scheduling

The compiler knows the pipeline, so it can move an independent instruction into the gap. The C code is `a = b + e; c = b + f;`. Addresses are in registers already.

```text
original                    scheduled
lw  x1, 0(x10)   # b        lw  x1, 0(x10)   # b
lw  x2, 0(x11)   # e        lw  x2, 0(x11)   # e
add x3, x1, x2   # stall    lw  x4, 0(x13)   # f   <- moved up
sw  x3, 0(x12)              add x3, x1, x2
lw  x4, 0(x13)   # f        sw  x3, 0(x12)
add x5, x1, x4   # stall    add x5, x1, x4
sw  x5, 0(x14)              sw  x5, 0(x14)
```

The original has two load-use stalls: 7 instructions take **13 cycles**. The scheduled version has none: **11 cycles** (7 instructions + 4 for fill). The result is identical. This is why `-O2` reorders instructions, and why compiler scheduling matters most on **in-order** cores such as the Arm Cortex-A53 and A55 and the many small RISC-V and microcontroller cores (a Cortex-M3 or M4 has a three-stage pipeline: fetch, decode, execute). An out-of-order core (tutorial 11) does this reordering itself in hardware.

---

## 7. Control hazards

A branch changes the PC, but the pipeline has already started fetching the instructions after it. With the branch decided in EX, a **taken** branch looks like this:

```text
cycle                       1    2    3    4    5    6    7    8
beq x1, x2, L              IF   ID   EX  MEM   WB
  wrong-path instr 1            IF   ID    X
  wrong-path instr 2                 IF    X
lw x5, 0(x6) (target)                     IF   ID   EX  MEM   WB
```

The two instructions fetched after the branch were the wrong ones and are flushed (`X`): **2 bubbles**, because the branch was decided two stages after it was fetched. A branch that is **not taken** costs nothing, since the instructions after it were the right ones (this is the policy "predict not taken").

The penalty is the number of stages between fetch and the place where the branch is decided:

| Branch decided in | Bubbles if taken | What it needs |
|---|---|---|
| **MEM** (many textbooks) | 3 | nothing extra |
| **EX** (this tutorial) | 2 | the comparison uses the ALU, the target uses an adder |
| **ID** | 1 | a comparator and a target adder in ID, and forwarding into ID. A branch that uses the result of the instruction just before it now stalls 1 cycle (and 2 cycles after a load) |

Deciding in ID saves a cycle only when the branch does not wait for the previous instruction. Jumps are similar: `jal` knows its target in ID (PC + immediate) and costs 1 bubble; `jalr`, which jumps to a register (a `ret`), needs the register value and costs more.

Ways to reduce the cost:

| Method | Idea |
|---|---|
| **Stall** until the branch is decided | simple, but every branch pays 2 or 3 cycles |
| **Predict not taken** (above) | free when right, flush when wrong |
| **Branch delay slot** | the instruction after the branch always runs, so the compiler fills it with useful work. Early MIPS and SPARC did this. Newer ISAs, including RISC-V and Arm, do not: a deeper pipeline would need more delay slots, and it would expose the pipeline in the ISA (tutorial 06) |
| **Dynamic branch prediction** | guess from history, usually right more than 95% of the time. This is tutorial 10 |

---

## 8. CPI and what stalls cost

With no hazards, one instruction finishes per cycle. Every stall cycle adds to the cost:

```text
CPI = 1 + (stall cycles per instruction)
```

And the iron law of tutorial 01 still holds: time per instruction = CPI x clock period. Pipelining shortens the clock a lot and raises CPI a little, so the product falls.

Example. Clock 220 ps. In the instruction mix, 25% are loads and 40% of loads are followed at once by an instruction that uses the value (1 stall each). 15% are branches and 60% of branches are taken (2 bubbles each, predict not taken). Nothing else stalls.

```text
CPI = 1 + 0.25 x 0.40 x 1 + 0.15 x 0.60 x 2
    = 1 + 0.10 + 0.18 = 1.28

time per instruction = 1.28 x 220 ps = 281.6 ps
vs single-cycle (800 ps):  800 / 281.6 = 2.84 times faster
vs multi-cycle (824 ps, tutorial 08):  824 / 281.6 = 2.93 times faster
```

A real run of this mix through the timing model gives CPI 1.28, matching the formula.

A loop shows it with real instructions. Each iteration has a load-use stall and a taken branch:

```text
loop: lw   x5, 0(x10)        # load a[i]
      add  x6, x6, x5        # uses x5 at once: 1 stall
      addi x10, x10, 4
      addi x7, x7, -1
      bne  x7, x0, loop      # taken: 2 bubbles
```

That is 5 instructions + 1 stall + 2 bubbles = **8 cycles per iteration** (CPI 1.6). Moving `add` two places down, behind the two `addi`s, removes the stall: **7 cycles** (CPI 1.4). A predictor that guesses the loop branch correctly removes the 2 bubbles (tutorial 10). Unrolling the loop (tutorial 11) amortizes the branch over more work.

> **Memory stalls come on top.** A cache miss stalls an in-order pipeline for the whole miss time, which can be tens or hundreds of cycles. So CPI = 1 + hazard stalls + memory stalls. Tutorial 13 computes the memory part.

---

## 9. Exceptions and interrupts in a pipeline

An **exception** is an unplanned change of control: a page fault, an illegal instruction, a system call, a timer interrupt. In a pipeline, five instructions are in flight when it happens, which makes it tricky. The requirement is a **precise exception**: when the handler starts,

1. every instruction **before** the faulting one has completed and written its results,
2. the faulting instruction and everything **after** it have had no effect on registers or memory,
3. the saved PC identifies the faulting instruction (for a fault) or the next instruction to run (for an interrupt).

The operating system depends on this. After fixing a page fault it returns, and the faulting instruction simply runs again.

Which stage can raise what:

| Stage | Possible exception |
|---|---|
| IF | instruction page fault, misaligned or bad fetch address |
| ID | illegal instruction, `ecall` (system call) |
| EX | arithmetic exception (x86 divide by zero, overflow on MIPS-style `add`; RISC-V has no trap for either) |
| MEM | data page fault, misaligned or illegal data access |
| WB | none |

How the pipeline stays precise:

1. When a stage detects an exception it does **not** act at once. It marks the instruction (a flag and a cause code) and the mark travels down the pipeline in the pipeline registers.
2. The instruction does no more harm: the mark turns off its register and memory writes.
3. When the marked instruction reaches the last stage that can fault (MEM), the pipeline acts: it saves the PC of that instruction (RISC-V: `mepc`) and the cause (`mcause`), **flushes every younger instruction** (turns them into bubbles), and loads the PC with the handler address.

Because everything is judged in program order at the end, the **oldest** exception wins. Example: a `lw` page-faults in MEM in the same cycle in which a younger instruction was found illegal in ID. The younger one is marked too, but the `lw` reaches MEM first, so the handler sees the `lw`'s fault and the younger instruction is simply flushed. If it was a real problem, it will be hit again when the program reruns.

An **interrupt** is not caused by any instruction. The pipeline chooses an instruction boundary: older instructions finish, younger ones are flushed, and the saved PC is the first instruction that did not complete, so the program resumes exactly there. The privilege levels, vectors and the cost of a system call are in [tutorial 26](26_interrupts_exceptions_and_dma.md).

**Imprecise exceptions** are what you get if instructions can finish out of order and nothing tracks program order: the exception may be reported after later instructions have already written their results, and restarting is almost impossible. Old machines with long floating-point pipelines had this problem. Modern general-purpose cores always give precise exceptions, using a **reorder buffer** in the out-of-order case (tutorial 11).

---

## 10. Long operations: multiply and divide

Not everything fits in one EX cycle. These latencies are our own illustrative numbers (real ones are in tutorial 12):

| Operation | Latency | Can start a new one every cycle? |
|---|---|---|
| integer ALU | 1 | yes |
| floating-point add | 4 | yes (the unit is pipelined) |
| floating-point multiply | 6 | yes |
| floating-point divide | 20 | no (busy for the whole time) |

Real cores today: an integer add takes 1 cycle, an integer multiply about 3, a floating-point add or multiply about 3 to 5, and a divide anywhere from about 10 to several dozen cycles depending on the core and the operand size.

This brings back old problems:

- **Structural hazard.** Two divides in a row: the second waits until the first finishes, because the unit is not pipelined.
- **Longer RAW stalls.** A dependent instruction waits for the whole latency. An `fmul` followed by a dependent `fadd` stalls 5 cycles.
- **WAW becomes real.** `fdiv.s f1, f2, f3` (20 cycles) followed by `fadd.s f1, f4, f5` (4 cycles): the add would finish first and the divide would then overwrite `f1` with the *older* value. The pipeline must stall or track this.
- **Out-of-order completion** makes precise exceptions harder (section 9), which is what the reorder buffer solves.

The important idea is **latency versus throughput of a unit**. A pipelined unit with latency 4 can start one operation per cycle, but a *chain* of dependent operations pays 4 cycles each:

```text
4 dependent multiplies, latency 4:    4 x 4 = 16 cycles
4 independent multiplies:             start in cycles 1, 2, 3, 4, the last finishes in cycle 7
```

This is why summing an array with four separate accumulators can be several times faster than one accumulator (tutorial 12).

---

## 11. How deep should a pipeline be?

Splitting the stages more finely shortens the clock. But every extra stage adds register overhead, and **a mistake costs more**: after a misprediction a deeper pipeline throws away more work. A toy model: the logic takes 800 ps in total, each pipeline register adds 20 ps, and 2% of instructions cause a pipeline flush costing about (depth - 1) cycles.

```text
time per instruction = (800 / depth + 20) x (1 + 0.02 x (depth - 1))
```

| Depth | Clock | CPI | Time per instruction |
|---|---|---|---|
| 5 | 180 ps | 1.08 | 194 ps |
| 10 | 100 ps | 1.18 | 118 ps |
| 20 | 60 ps | 1.38 | 83 ps |
| 40 | 40 ps | 1.78 | 71 ps |
| 80 | 30 ps | 2.58 | 77 ps |
| 120 | 27 ps | 3.38 | 90 ps |

The gains flatten, the best is around 40 to 50 stages in this toy, and past it deeper is slower. The toy even ignores two more reasons that stop real designs much earlier: **power** (more registers and a faster clock mean more switching, [tutorial 28](28_performance_and_power.md)) and **memory** (a cache miss takes the same time in nanoseconds, so a faster clock means more cycles lost). The Pentium 4 "Prescott" core went to 31 stages and the industry then went back to shorter pipelines. Current big cores have a branch misprediction penalty of roughly 15 to 20 cycles, and the number of cycles a branch mistake costs is one of the most important numbers in a core (tutorial 10).

---

## 12. What this means for your code and for real cores

- **Predictable branches are cheap.** A mispredicted branch flushes the pipeline (tutorial 10).
- **Dependency chains are slow, independent work is fast.** Each link of a chain pays the full latency of its operation (tutorial 12).
- **A value loaded and used at once waits.** Keep a little distance between a load and its use. Compilers do this for you, as in section 6.5.
- **Real cores go much further.** They fetch and issue several instructions per cycle (*superscalar*), execute them out of order, and predict branches long before they are resolved. Tutorials 10, 11 and 12 build that on the five-stage skeleton you now know.

---

## 13. Mistakes that cost points

1. **Saying pipelining makes each instruction faster.** It raises throughput; the latency of one instruction slightly grows.
2. **"A k-stage pipeline is k times faster."** That is only the ideal limit. Unbalanced stages, register overhead and hazards bring it down (section 3).
3. **Forgetting the load-use stall.** Even with full forwarding, a `lw` followed at once by a user of its value costs one cycle.
4. **Forwarding wrongly.** Forgetting `RegWrite`, forwarding for `x0`, or letting an older value beat a newer one.
5. **Writing back with the wrong `rd`.** The destination register must travel in the pipeline registers.
6. **Saying WAR and WAW are problems in the five-stage pipeline.** They are not (section 6.4). They matter for out-of-order execution and long-latency units.
7. **Counting the branch penalty from the wrong stage.** It is the number of stages between fetch and the place the branch is decided: 2 bubbles for EX, 1 for ID.
8. **Taking a branch penalty for a branch that was predicted correctly.** A correct prediction costs nothing.

---

## 14. Questions you will be asked

**What is pipelining, and what does it improve?** Overlapping the execution of several instructions, each in a different stage. It improves throughput (instructions per second); latency of one instruction does not improve.

**Why is the speedup less than the number of stages?** Unbalanced stage delays (the slowest sets the clock), pipeline register overhead, and hazard stalls.

**Name the three kinds of hazard and a cure for each.** Structural (two instructions need the same hardware): duplicate it, as in split instruction and data caches. Data (a value is not ready): forwarding, then a stall for load-use, or better instruction order. Control (the next PC is unknown): prediction, deciding earlier, flushing.

**How does forwarding work?** Extra wires and multiplexers send a result from the EX/MEM or MEM/WB pipeline register straight to the ALU input of the next instruction, so it does not wait for the register file write. A comparator checks that the register numbers match, that `RegWrite` is set and that the register is not `x0`.

**Why does a load followed by a dependent instruction still stall?** The loaded data exists only at the end of MEM, one stage after the ALU result would be available. A wire cannot deliver it before it exists, so one bubble is needed.

**Which data hazards occur in the classic five-stage pipeline?** Only RAW. Reads happen early (ID) and writes late (WB), in program order, so WAR and WAW cannot go wrong.

**What does a taken branch cost?** The number of stages between fetch and resolution: 2 bubbles when decided in EX, 1 in ID. Predicting correctly makes it 0.

**What is a precise exception and how do you get one?** At the handler, earlier instructions are done and later ones had no effect. Carry the exception with the instruction, act on it in program order when it reaches the end, flush all younger instructions, and save the faulting PC.

**Why not make a pipeline 50 stages deep?** Register overhead grows, a misprediction throws away more work, the clock needs more power, and a cache miss costs more cycles at a faster clock. The gain flattens and then reverses.

**What is the difference between a stall, a bubble and a flush?** A stall holds instructions back; a bubble is the empty slot that results; a flush discards wrongly fetched instructions, turning them into bubbles.

**How do compilers help a pipeline?** They schedule independent instructions into the gaps, such as moving a load earlier so its use is not right behind it. This matters most on in-order cores.

---

## 15. Problems

**How to use this section.** Try each problem before you read the answers at the bottom. All four are paper problems (about 20 to 25 minutes each). Draw the diagrams on squared paper: one row per instruction, one column per cycle. Assume the register file writes in the first half of a cycle and reads in the second half.

### Problem A (paper): pipeline diagrams

```text
lw  x1, 0(x2)
add x3, x1, x4
sub x5, x3, x1
sw  x5, 4(x2)
and x6, x7, x8
```

(a) List every RAW dependence (the register, the producer and the consumer).
(b) Draw the pipeline with **no forwarding**. How many cycles does the sequence take?
(c) Draw it with **full forwarding** and the load-use stall. How many cycles? For each dependence name the forwarding path that is used, or say that none is needed.

*Where:* paper. *How:* sections 4 and 6. Without forwarding, a consumer cannot leave ID before the cycle in which its producer is in WB. With forwarding, only a load followed at once by its user stalls.

### Problem B (paper): clock, latency and speedup

Stage delays: IF 180 ps, ID 120 ps, EX 160 ps, MEM 220 ps, WB 100 ps. The pipeline registers add 30 ps.
(a) The clock period of the single-cycle design, the clock period of the 5-stage pipeline, and the latency of one instruction in the pipeline.
(b) The time for 10,000 instructions on each (assume no stalls) and the speedup. Why is it not 5?
(c) Split MEM into two stages of 110 ps each (a 6-stage pipeline). The new clock, the new speedup, and one thing that gets worse.

*Where:* paper and a calculator. *How:* section 3: clock = slowest stage + overhead; time = (stages + N - 1) x clock.

### Problem C (paper): CPI

The clock is the 250 ps from Problem B. The mix is 50% ALU, 20% loads, 10% stores, 20% branches. 30% of loads are followed at once by an instruction that uses the loaded value (1 stall each). Half of the branches are taken. Forwarding exists; branches are decided in EX; the policy is predict not taken.
(a) The CPI, the time per instruction, and the speedup over the single-cycle machine of Problem B (780 ps).
(b) Now decide branches in ID. A taken branch costs 1 bubble, but 40% of branches use the result of the ALU instruction right before them and stall 1 cycle. The new CPI. Is it better, and what must be true for the change to pay off?
(c) Remove forwarding altogether (branches still decided in EX). 35% of instructions use the result of the instruction right before (2 stalls each) and 15% the result of the one before that (1 stall each); the load-use stall is already inside those percentages. The CPI, and how much slower than (a).

*Where:* paper. *How:* section 8: CPI = 1 + sum of (frequency x probability x stall cycles).

### Problem D (paper): schedule and exception

```text
lw  x1, 0(x10)
lw  x2, 4(x10)
add x3, x1, x2
lw  x4, 8(x10)
sub x5, x1, x4
sw  x3, 0(x11)
sw  x5, 4(x11)
```

(a) With forwarding, where are the stalls, and how many cycles does the code take?
(b) Reorder the instructions (same result) to remove every stall. How many cycles?
(c) In your reordered code the third load (`lw x4, 8(x10)`) page-faults in MEM. Which instructions have completed, which are flushed, what does the saved PC point at, and what happens when the operating system returns?

*Where:* paper. *How:* (a) and (b) sections 6.3 and 6.5; (c) section 9, and find which stage each instruction is in during the cycle when the `lw` is in MEM.

---

## Answers

Try before reading.

### Problem A

(a) `x1`: `lw` to `add`, and `lw` to `sub`. `x3`: `add` to `sub`. `x5`: `sub` to `sw`.

(b) No forwarding, **15 cycles**. `add` waits until `lw` is in WB (cycle 5), `sub` until `add` is in WB (cycle 8), `sw` until `sub` is in WB (cycle 11); `and` follows without extra delay (the starred names are stalls):

```text
cycle                     1    2    3    4    5    6    7    8    9   10   11   12   13   14   15
lw x1, 0(x2)             IF   ID   EX  MEM   WB
add x3, x1, x4                IF  ID*  ID*   ID   EX  MEM   WB
sub x5, x3, x1                     IF  IF*  IF*  ID*  ID*   ID   EX  MEM   WB
sw x5, 4(x2)                                      IF  IF*  IF*  ID*  ID*   ID   EX  MEM   WB
and x6, x7, x8                                                   IF  IF*  IF*   ID   EX  MEM   WB
```

(c) With forwarding, **10 cycles**, with one stall:

```text
cycle                     1    2    3    4    5    6    7    8    9   10
lw x1, 0(x2)             IF   ID   EX  MEM   WB
add x3, x1, x4                IF  ID*   ID   EX  MEM   WB
sub x5, x3, x1                     IF  IF*   ID   EX  MEM   WB
sw x5, 4(x2)                                 IF   ID   EX  MEM   WB
and x6, x7, x8                                    IF   ID   EX  MEM   WB
```

The paths: `lw` to `add`: MEM/WB to the ALU input, after the one load-use stall. `add` to `sub`: EX/MEM to the ALU input. `lw` to `sub`: none needed, because `sub` is in ID in the same cycle as `lw`'s WB and reads the new value from the register file. `sub` to `sw`: MEM/WB to the **store data** input in MEM (a store needs its data only in MEM, so there is no stall).

### Problem B

(a) Single-cycle: 180 + 120 + 160 + 220 + 100 = **780 ps**. Pipelined: slowest stage 220 + overhead 30 = **250 ps**. Latency in the pipeline: 5 x 250 = **1,250 ps** (longer than 780).

(b) Single-cycle: 10,000 x 780 = **7,800,000 ps**. Pipelined: (5 + 10,000 - 1) x 250 = 10,004 x 250 = **2,501,000 ps**. Speedup = 7,800,000 / 2,501,000 = **3.12**. It is not 5 because the stages are unbalanced (the slowest, 220, is far above the average of 156) and because the register overhead of 30 ps is paid in every cycle.

(c) The stages become 180, 120, 160, 110, 110, 100. The slowest is now IF (180), so the clock is 180 + 30 = **210 ps**. Time = (6 + 10,000 - 1) x 210 = 10,005 x 210 = 2,101,050 ps, speedup 7,800,000 / 2,101,050 = **3.71**. What gets worse: a loaded value now appears one stage later, so the load-use stall grows from 1 cycle to 2; the latency of one instruction grows to 1,260 ps; there are more pipeline registers (power and area).

### Problem C

(a) CPI = 1 + 0.20 x 0.30 x 1 + 0.20 x 0.50 x 2 = 1 + 0.06 + 0.20 = **1.26**. Time per instruction = 1.26 x 250 = **315 ps**. Speedup over the single-cycle machine = 780 / 315 = **2.48**.

(b) CPI = 1 + 0.06 + 0.20 x 0.50 x 1 + 0.20 x 0.40 x 1 = 1 + 0.06 + 0.10 + 0.08 = **1.24**. It is only 1.6% better (1.26 to 1.24). It pays off only if moving the comparison and the extra forwarding into ID does not lengthen the clock by more than about 1.6% (4 ps of 250). ID now holds the register read, the forwarding muxes and a comparator, so the clock probably gets longer: it may well lose.

(c) CPI = 1 + 0.35 x 2 + 0.15 x 1 + 0.20 x 0.50 x 2 = 1 + 0.70 + 0.15 + 0.20 = **2.05**. Time per instruction = 2.05 x 250 = 512.5 ps, which is 2.05 / 1.26 = **1.63 times slower** than (a). Forwarding is worth a lot of hardware.

### Problem D

(a) With forwarding, `add x3` follows `lw x2` at once (1 stall) and `sub x5` follows `lw x4` at once (1 stall). Total: 7 + 4 (fill) + 2 = **13 cycles**:

```text
cycle                     1    2    3    4    5    6    7    8    9   10   11   12   13
lw x1, 0(x10)            IF   ID   EX  MEM   WB
lw x2, 4(x10)                 IF   ID   EX  MEM   WB
add x3, x1, x2                     IF  ID*   ID   EX  MEM   WB
lw x4, 8(x10)                           IF  IF*   ID   EX  MEM   WB
sub x5, x1, x4                                    IF  ID*   ID   EX  MEM   WB
sw x3, 0(x11)                                          IF  IF*   ID   EX  MEM   WB
sw x5, 4(x11)                                                    IF   ID   EX  MEM   WB
```

(b) Move `lw x4` up in front of the `add`, so each load is followed by an instruction that does not use it. One order: `lw x1`, `lw x2`, `lw x4`, `add x3`, `sub x5`, `sw x3`, `sw x5`. No stalls, 7 + 4 = **11 cycles**:

```text
cycle                     1    2    3    4    5    6    7    8    9   10   11
lw x1, 0(x10)            IF   ID   EX  MEM   WB
lw x2, 4(x10)                 IF   ID   EX  MEM   WB
lw x4, 8(x10)                      IF   ID   EX  MEM   WB
add x3, x1, x2                          IF   ID   EX  MEM   WB
sub x5, x1, x4                               IF   ID   EX  MEM   WB
sw x3, 0(x11)                                     IF   ID   EX  MEM   WB
sw x5, 4(x11)                                          IF   ID   EX  MEM   WB
```

(c) In cycle 6 the faulting `lw x4` is in MEM. At that moment `lw x1` finished in cycle 5 and `lw x2` is in WB, so both **complete**. The `add x3` is in EX, the `sub x5` in ID and the first `sw` in IF: they are **flushed** (turned into bubbles), so `x3`, `x5` and memory are untouched. The saved PC (`mepc`) holds the address of **`lw x4`**, the faulting instruction. The operating system maps the page and executes the return-from-exception instruction. The pipeline refetches from `lw x4`, and `add`, `sub` and both stores run again. The final state is exactly what a run without the fault produces. That is a precise exception.

---

## Cheat sheet

- **Stages:** IF, ID, EX, MEM, WB, separated by pipeline registers (IF/ID, ID/EX, EX/MEM, MEM/WB). Control signals and `rd` travel along. The register file writes in the first half of a cycle and reads in the second.
- **Speed:** clock = slowest stage + register overhead; time = (stages + N - 1) x clock; throughput rises, latency of one instruction does not. Speedup is below the stage count because of unbalanced stages, overhead and hazards.
- **Hazards:** structural (hardware clash: duplicate or stall), data (forward or stall or reorder), control (predict, decide earlier, flush).
- **Forwarding:** EX/MEM and MEM/WB feed the ALU inputs; check `RegWrite`, `rd` not `x0`, and prefer the newest value. Store data is forwarded into MEM.
- **Load-use:** the one case forwarding cannot fix: 1 stall (hazard detection: `ID/EX.MemRead` and a register match; freeze PC and IF/ID, insert a bubble).
- **WAR, WAW:** impossible in the in-order 5-stage pipeline; they appear with out-of-order execution and long-latency units.
- **Branches:** bubbles = stages between fetch and the decision: 2 in EX, 1 in ID. Predict not taken: a correct guess is free. Delay slots are obsolete; dynamic prediction (tutorial 10) is the real answer.
- **CPI** = 1 + stalls per instruction (+ memory stalls). Time per instruction = CPI x clock.
- **Precise exceptions:** carry the exception with the instruction, act on it in order at the end, flush younger instructions, save the faulting PC. Oldest exception wins.
- **Long operations:** latency (paid by dependent chains) versus throughput (one per cycle if pipelined). Independent work hides latency.
- **Depth:** gains flatten and then reverse (register overhead, misprediction cost, power, memory latency).

**Next:** tutorial 10, Branch Prediction (static and dynamic predictors, 2-bit counters, the branch target buffer, history-based predictors, the return address stack, and what a misprediction costs). Say "next" when you are ready.
