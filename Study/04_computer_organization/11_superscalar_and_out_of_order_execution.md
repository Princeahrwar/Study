# CO 11: Superscalar and Out-of-Order Execution

*Part of the [Computer Organization plan](00_plan.md) · Core tutorial · Time: about 4 hours (2 reading, 2 problems) · You need: [tutorial 09](09_pipelining_and_hazards.md) (the pipeline, hazards, long operations, precise exceptions) and [tutorial 10](10_branch_prediction.md) (prediction and what a miss costs)*

By the end of this tutorial you can:

- explain instruction-level parallelism (ILP), find the critical path of a piece of code, and say why a dependency chain limits any machine;
- explain what a superscalar core is, and why an in-order one wastes most of its width;
- trace the same code on an in-order machine, a scoreboard and Tomasulo's algorithm, and explain every difference;
- explain register renaming with a physical register file, and say which dependences it removes and which it cannot;
- explain the reorder buffer: in-order commit, precise exceptions, and how a misprediction is undone;
- explain the load/store queue: store-to-load forwarding, memory disambiguation, and why stores wait until commit;
- explain memory-level parallelism, and why a pointer chase is slow while an array walk is not;
- compare the compiler's way to find ILP (unrolling, software pipelining, trace scheduling) and VLIW/EPIC with the hardware's way.

In [tutorial 09](09_pipelining_and_hazards.md) the pipeline was **in order** and finished at most **one instruction per cycle**. Both limits hurt. A slow instruction (a divide, a cache miss) stops every instruction behind it, even if those do not need its result; and one instruction per cycle is a hard ceiling. This tutorial removes both limits. Tutorial 12 then draws a real core with real numbers, and all of its parts are explained here. The cycle counts in the tables were produced by a small timing model of the three machines, and the C timings in section 8 were measured.

> **The one-sentence model.** A modern core takes instructions **in program order**, executes them **in whatever order their inputs become ready**, and retires them **in program order** so that the program cannot tell the difference.

---

## 1. Instruction-level parallelism

### 1.1 Three kinds of dependence

Two instructions can run at the same time only if one does not depend on the other. Tutorial 09 named the dependences; here they are again with one new name.

| Kind | Example | Real? |
|---|---|---|
| **True (RAW)**: reads a value the earlier one writes | `add x1,..` then `sub x4, x1,..` | yes: the value must exist first |
| **Name (WAR, WAW)**: reuses a register name | `sub x4, x1, x5` then `add x1, x6, x7` (WAR on `x1`) | no: only the *name* is shared |
| **Control**: whether the instruction runs at all depends on a branch | everything after a `beq` | handled by prediction (tutorial 10) |

True dependences are the only ones that cannot be removed. Name dependences are an accident of having few registers, and section 5 removes them. Control dependences are guessed away.

### 1.2 The dataflow graph and the critical path

Draw one node per instruction and an arrow for each true dependence. The longest path through the graph (the **critical path**) is the least time the code can take on any machine, however wide.

```text
1:  a = b + c          2 needs 1
2:  d = a * 2          4 needs 3
3:  e = f + g          5 needs 2 and 4
4:  h = e * 3
5:  i = d + h
```

```text
level 1:   1    3          (independent: can run together)
level 2:   2    4
level 3:     5
```

With every operation taking 1 cycle, the critical path is 3, so no machine finishes in fewer than 3 cycles. The **ILP** of this code is 5 instructions / 3 cycles = **1.67**. A machine that issues 1 instruction per cycle needs 5 cycles; one that issues 2 or any larger number needs 3 (the model agrees: 5, 3 and 3). Extra width beyond the ILP is wasted.

### 1.3 A chain you cannot widen away

```c
for (i = 0; i < n; i++) s += a[i];
```

Every add needs the `s` of the previous iteration. That is a chain of n adds. With a floating-point add latency of 4 cycles (tutorial 09), the loop takes at least 4n cycles on any machine, because the loop's critical path is 4 cycles per iteration. The loads and the counter are independent and run far ahead; the chain is what limits the loop. Breaking the chain is a code change, covered in section 10.

---

## 2. Superscalar: more than one instruction per cycle

A **superscalar** core can issue (and finish) up to **w** instructions in the same cycle, where w is the **width**. The ideal CPI becomes 1/w: a 4-wide core aims for CPI 0.25, which is the number used in tutorial 10.

The simplest design is an **in-order superscalar**: take instructions in program order, and in each cycle issue the first one, then the next ones as long as they are independent of everything issued in that cycle, in order. At the first instruction that cannot go, stop.

```text
1:  add x1, x2, x3
2:  sub x4, x1, x5        needs 1
3:  add x6, x7, x8
4:  and x9, x6, x10       needs 3
```

On a 2-wide in-order core:

| Cycle | Issued | Why |
|---|---|---|
| 1 | 1 | 2 needs 1, so it cannot go in the same cycle, and nothing behind it may jump ahead |
| 2 | 2, 3 | independent |
| 3 | 4 | 4 needs 3, so it could not go in cycle 2 together with it; it is also the last instruction |

Three cycles for four instructions: IPC 1.33, not 2. A machine that may pick *any* ready instruction from the first four runs {1, 3} in cycle 1 and {2, 4} in cycle 2: **2 cycles**, IPC 2. That machine is out of order.

In-order superscalars exist and are common where power and area matter: many small cores, such as the Arm Cortex-A53 and A55 found in many network and Wi-Fi SoCs, issue two instructions per cycle in order. Their compilers must schedule instructions well (section 10), because the hardware will not rescue a bad order.

What stops a wide in-order core: the first blocked instruction (a cache miss, a divide, a dependency) blocks everything behind it. Section 4 shows how much that costs.

---

## 3. The shape of an out-of-order core

An out-of-order (OoO) core has three parts in a row. Only the middle one is out of order.

```text
   IN ORDER                          OUT OF ORDER                      IN ORDER
 fetch, decode,    allocate     +--------------------+  execute,    +---------------+
 predict branches --> rename --->|  scheduler         |--> write --->|  reorder      |--> commit
 (tutorial 10)       (in ROB)    |  (waiting instrs)  |    result    |  buffer (ROB) |    (retire)
                                 +--------------------+              +---------------+
```

1. **Front end (in order).** Fetches along the *predicted* path, decodes, **renames** the registers (section 5) and **allocates** each instruction an entry in the reorder buffer (section 6), in program order.
2. **Scheduler (out of order).** Instructions wait here until their operands exist. Each cycle the scheduler picks ready instructions and sends them to free execution units. A finished instruction **wakes up** the waiting instructions that need its result.
3. **Commit (in order).** The oldest instructions leave the reorder buffer one after another, making their results permanent.

**Words.** Textbooks and vendors use these terms in different ways. In the next sections, *allocate* is entering the ROB and scheduler, *issue* is the first step of an instruction in the tables (taking a place in a reservation station or functional unit), *write result* is producing the value, and *commit* (or *retire*) is making it final. For sending a micro-op to an execution port, Intel's manuals say *dispatch* and Arm's manuals say *issue*. Check the glossary of whatever manual you read.

---

## 4. One program, three machines

To compare machines fairly, we run the same 8 instructions through all three. **Rules used in every table** (these are illustrative numbers, smaller than real ones, to keep the tables short):

- one instruction enters per cycle, in order, starting in cycle 1;
- latencies: `fld` 2, `fadd` 2, `fmul` 4, `fdiv` 8; there are enough units of every kind;
- an instruction starts executing in the cycle **after** both operands have been written (a result written in cycle t can be used in cycle t + 1), and never earlier than the cycle after it was issued;
- it writes its result in the cycle after its last execution cycle;
- there is **one result bus**: one write per cycle. If two want the same cycle, the older goes first and the other writes a cycle later.

```text
1  fld   f1, 0(x1)       load, 2
2  fmul  f2, f1, f0      needs 1 (f1)
3  fadd  f3, f1, f4      needs 1 (f1)
4  fdiv  f5, f2, f3      needs 2 and 3
5  fadd  f6, f5, f4      needs 4 (f5)
6  fmul  f2, f7, f8      independent, but reuses the name f2
7  fadd  f9, f2, f4      needs 6 (the NEW f2)
8  fmul  f10, f9, f8     needs 7
```

Instructions 1 to 5 form a chain around the slow divide. Instructions 6 to 8 are a separate chain. Instruction 6 reuses the register `f2`, which instruction 2 writes (WAW) and instruction 4 reads (WAR).

The cycle in which each instruction writes its result:

| # | Instruction | In-order | Scoreboard | Tomasulo |
|---|---|---|---|---|
| 1 | `fld f1` | 4 | 4 | 4 |
| 2 | `fmul f2` | 9 | 9 | 9 |
| 3 | `fadd f3` | 8 | 7 | 7 |
| 4 | `fdiv f5` | 18 | 18 | 18 |
| 5 | `fadd f6` | 21 | 21 | 21 |
| 6 | `fmul f2` | 24 | 15 | 11 |
| 7 | `fadd f9` | 27 | 19 | 14 |
| 8 | `fmul f10` | 32 | 24 | 19 |
| | **All results written by** | **32** | **24** | **21** |

The longest chain is 1, 2, 4, 5: each step costs its latency plus the write cycle, so the dataflow limit of this code is cycle 21. **Tomasulo reaches it**: instructions 6 to 8 hide completely in the shadow of the divide. Details below.

### 4.1 The in-order machine

Rules: an instruction may start only when all earlier instructions have started, its operands are written, and it will not write its register before an earlier instruction that writes the same register. Results may come out of order (tutorial 09, section 10).

Instruction 5 needs the divide's result and starts in cycle 19. Instructions 6, 7 and 8 do not need the divide at all, but they sit behind instruction 5 and cannot start earlier: 6 starts in cycle 20, 7 waits for 6 and starts in 25, 8 starts in 28. **Total: 32 cycles.** The whole second chain happens *after* the first one, although the two are independent.

### 4.2 The scoreboard

The scoreboard (first used in the CDC 6600) is the first step: it lets independent instructions go ahead. It keeps a table with the state of every functional unit and every register, and each instruction passes four steps:

| Step | Waits for | If it must wait |
|---|---|---|
| **Issue** (in order) | a free functional unit, and no earlier instruction still going to write the same register (**WAW**) | everything behind it waits too, because issue is in order |
| **Read operands** | both operands written (**RAW**) | only this instruction waits, inside its unit |
| **Execute** | nothing | |
| **Write result** | no earlier instruction still has to read the old value of this register (**WAR**) | the result waits in the unit |

The trace ("start" = the cycle the operands are read and execution begins, "end" = the last execute cycle):

| # | Instruction | Issue | Start | End | Write |
|---|---|---|---|---|---|
| 1 | `fld f1` | 1 | 2 | 3 | 4 |
| 2 | `fmul f2` | 2 | 5 | 8 | 9 |
| 3 | `fadd f3` | 3 | 5 | 6 | 7 |
| 4 | `fdiv f5` | 4 | 10 | 17 | 18 |
| 5 | `fadd f6` | 5 | 19 | 20 | 21 |
| 6 | `fmul f2` | **10** | 11 | 14 | 15 |
| 7 | `fadd f9` | 11 | 16 | 17 | 19 |
| 8 | `fmul f10` | 12 | 20 | 23 | 24 |

Instructions 2 and 3 now run side by side, so instruction 3 is a cycle earlier than in-order. The weakness shows at instruction 6: it writes `f2`, and instruction 2 (still running, until cycle 9) also writes `f2`, so instruction 6 cannot even **issue** until cycle 10. Instructions 7 and 8 are stuck behind it. Instruction 7 wanted to write in cycle 18, the same cycle as the older divide, so it writes in 19. **Total: 24.** The scoreboard cannot get rid of the name dependence; it can only wait for it.

### 4.3 Tomasulo's algorithm

Tomasulo's algorithm (first used in the IBM 360/91) removes the name dependences by **renaming through tags**, and passes results straight to the instructions that wait for them. The pieces:

```text
Reservation station (RS): one per waiting instruction, sits in front of a functional unit
    op | Vj | Vk | Qj | Qk
    Vj, Vk : the operand VALUES, if already known
    Qj, Qk : TAGS (the name of the RS that will produce the value), if not known yet

Register status: one entry per architectural register
    the tag of the RS that will write it next (empty = the register file is up to date)

Common data bus (CDB): a broadcast bus. A finished unit sends (tag, value) to everyone.
```

The three steps:

1. **Issue** (in order). Take a free RS. For each source: if the register status is empty, copy the value; otherwise copy the *tag* of the RS that will produce it. Then set the register status of the destination to **this RS's tag**.
2. **Execute.** When both Q fields are empty (both values present), the instruction runs.
3. **Write result.** Broadcast (tag, value) on the CDB. Every RS holding that tag captures the value. A register is written **only if its status still holds this tag.** One result per cycle.

The key is the register status. When instruction 6 issues in cycle 6 it sets `status[f2] = RS6`, replacing `RS2`. Instruction 4, issued earlier, still waits for `RS2` and gets its value from the bus. The two uses of the name `f2` no longer meet. A snapshot at the end of cycle 6:

| RS | Instruction | Vj | Vk | Qj | Qk | State |
|---|---|---|---|---|---|---|
| RS2 | `fmul f2, f1, f0` | f1 | f0 | – | – | running (cycles 5 to 8) |
| RS3 | `fadd f3, f1, f4` | f1 | f4 | – | – | running (5 to 6) |
| RS4 | `fdiv f5, f2, f3` | – | – | **RS2** | RS3 | waiting for two tags |
| RS5 | `fadd f6, f5, f4` | – | f4 | RS4 | – | waiting for one tag |
| RS6 | `fmul f2, f7, f8` | f7 | f8 | – | – | ready; starts in cycle 7 |

Register status: `f2 → RS6`, `f3 → RS3`, `f5 → RS4`, `f6 → RS5`; the others are empty.

What happens next: in cycle 7, RS3 writes; RS4 captures the value for `Qk` and the register `f3` is updated (its status was RS3). In cycle 9, RS2 writes; RS4 captures it for `Qj`, but **`f2` is not written**, because its status says RS6 now. In cycle 11, RS6 writes and `f2` is updated.

The full trace:

| # | Instruction | Issue | Execute | Write | Commit |
|---|---|---|---|---|---|
| 1 | `fld f1` | 1 | 2 to 3 | 4 | 5 |
| 2 | `fmul f2` | 2 | 5 to 8 | 9 | 10 |
| 3 | `fadd f3` | 3 | 5 to 6 | 7 | 11 |
| 4 | `fdiv f5` | 4 | 10 to 17 | 18 | 19 |
| 5 | `fadd f6` | 5 | 19 to 20 | 21 | 22 |
| 6 | `fmul f2` | 6 | 7 to 10 | 11 | 23 |
| 7 | `fadd f9` | 7 | 12 to 13 | 14 | 24 |
| 8 | `fmul f10` | 8 | 15 to 18 | 19 | 25 |

Instruction 6 issues at cycle 6, runs at once and writes in cycle 11, 22 cycles earlier than in the in-order machine. **Total: 21.** The "Commit" column belongs to section 6: results are produced out of order but made permanent in order.

**Classic Tomasulo and today's cores.** The original version wrote results straight into the registers, so an exception could leave the machine in a state that no sequential execution produces (an *imprecise* exception). Modern cores keep Tomasulo's two ideas (waiting instructions, results delivered by tag, renaming) and add the reorder buffer (section 6) for in-order commit.

---

## 5. Register renaming with a physical register file

Modern cores rename differently from the original Tomasulo design. They keep a **physical register file (PRF)** much bigger than the architectural register set (hundreds of entries versus 16 or 32), and a table that says which physical register currently holds each architectural register.

| Piece | Job |
|---|---|
| **Register alias table (RAT)** | maps each architectural register to a physical register: the *newest* mapping |
| **Free list** | the physical registers that nobody uses |
| **Physical register file** | the values; every value lives in exactly one place |

Renaming one instruction: read the RAT for the sources, take a **new** physical register from the free list for the destination, write that into the RAT, and remember the *old* mapping of the destination (it is needed for commit and for recovery). The same program as section 4, with `f0` to `f31` starting in `p0` to `p31` and the free list starting at `p32`:

| # | Original | Renamed | Old mapping of the destination |
|---|---|---|---|
| 1 | `fld f1, 0(x1)` | `fld p32, 0(x1)` | `p1` |
| 2 | `fmul f2, f1, f0` | `fmul p33, p32, p0` | `p2` |
| 3 | `fadd f3, f1, f4` | `fadd p34, p32, p4` | `p3` |
| 4 | `fdiv f5, f2, f3` | `fdiv p35, p33, p34` | `p5` |
| 5 | `fadd f6, f5, f4` | `fadd p36, p35, p4` | `p6` |
| 6 | `fmul f2, f7, f8` | `fmul p37, p7, p8` | `p33` |
| 7 | `fadd f9, f2, f4` | `fadd p38, p37, p4` | `p9` |
| 8 | `fmul f10, f9, f8` | `fmul p39, p38, p8` | `p10` |

Look at `f2`: instructions 2 and 6 both write it, but they write **different physical registers** (`p33` and `p37`). Instruction 4 reads `p33` (the old value), instruction 7 reads `p37` (the new one). The WAW and WAR dependences are gone; only the true dependences remain.

**When is a physical register freed?** When the instruction that *replaced* its mapping **commits**. `p33` can be freed when instruction 6 commits: from then on no older instruction can still need it. (If it were freed earlier, an older instruction waiting for `p33` could read garbage, and after a misprediction the old mapping might be needed again.)

**What renaming can and cannot do.**

- It removes WAR and WAW completely, because every write gets a fresh name.
- It cannot remove RAW. The value must still be computed first.
- It runs out: when the free list is empty, the front end **stalls**. The number of physical registers bounds the number of instructions in flight that have a destination. This is one of the limits on how far ahead the core can look.
- The register file of an x86 core has hundreds of entries even though the ISA has 16 general registers. This is why "reuse the same variable" never costs performance in compiled code.

---

## 6. The reorder buffer: in-order commit and precise exceptions

Renaming lets instructions finish out of order. The **reorder buffer (ROB)** puts the order back. It is a circular queue with one entry per in-flight instruction, **allocated in program order**:

```text
head (oldest)  ───────────────────────────────────────────────►  tail (newest)
 | PC / type | dest arch reg | new phys reg | OLD phys reg | done? | exception? | branch info |
```

- The **tail** receives each new instruction at allocation.
- When an instruction finishes, it sets its **done** flag (and its **exception** flag, if it faulted).
- The **head** commits when it is done: its old physical register is freed, and a store is released toward memory (section 7). Several instructions can commit per cycle.

An instruction is *speculative* until it commits. The registers visible to the program are those of the **committed** state. A snapshot of the section 4 program at the end of cycle 12:

| # | Instruction | State |
|---|---|---|
| 1, 2, 3 | | committed (in cycles 5, 10, 11) |
| 4 | `fdiv f5` | **head**, still executing |
| 5 | `fadd f6` | waiting in the scheduler |
| 6 | `fmul f2` | **done** (wrote in cycle 11) but cannot commit, because 4 and 5 are older |
| 7 | `fadd f9` | executing |
| 8 | `fmul f10` | waiting in the scheduler |

Instruction 6 finished first and then waited until cycle 23 to commit. That is the price of in-order commit, and the ROB is where the waiting happens.

### 6.1 Precise exceptions

An instruction that faults (a page fault on the `fld`, say) sets its exception flag when it executes. **Nothing happens yet.** Only when it reaches the head of the ROB does the core act: everything younger is discarded, the registers are the committed ones, and the handler runs with the faulting PC saved. All older instructions are complete and no younger instruction had any visible effect: a **precise exception** (tutorial 09, section 9). If an older instruction faults later in time but earlier in program order, it still wins, because the head is always the oldest. The same holds for an interrupt: it is taken at the head.

### 6.2 Undoing a misprediction

A branch resolves when it **executes**, usually long before it commits. If it was predicted wrongly, the core:

1. redirects fetch to the correct address;
2. **flushes every instruction younger than the branch** from the ROB, the scheduler and the load/store queues;
3. **restores the RAT** to its state at the branch, and returns the physical registers of the flushed instructions to the free list;
4. refills the front end.

Step 3 can be done in three ways: restore a **checkpoint** of the RAT taken at each branch (fast, costs storage); **walk back** through the ROB undoing mappings with the saved old mappings (slower, no extra storage); or wait until the branch commits and copy the committed RAT (simple and slow). Big cores use checkpoints for branches. The penalty in tutorial 10 (about 12 to 20 cycles) is mostly the **front-end refill**, not the time to clean the ROB. Instructions older than the branch are not touched and keep running.

**Wrong-path work leaves traces.** The flushed instructions never became architectural, but a flushed load may already have brought a line into the cache. This is the root of Spectre (tutorial 10, section 12, and tutorial 30).

---

## 7. Memory instructions in an out-of-order core

Registers can be renamed because their names are known when the instruction is decoded. Memory addresses are not known until they are computed. This makes loads and stores harder.

### 7.1 The load queue and the store queue

Two queues, also in program order, track the memory instructions:

- **Stores** compute their address and data out of order, but they **do not write the cache until they commit**. A store is speculative until then, and another core must never see a speculative store. After commit, the store sits in the **store buffer** and drains to the L1 cache in order. (The store buffer is also the source of x86's one reordering, store before a later load; tutorial 20.)
- **Loads** execute as early as possible. A load must see the **newest older store to the same address**, so it checks the store queue before it goes to the cache.

### 7.2 Store-to-load forwarding

```text
sw  x5, 0(x10)        store 4 bytes
lw  x6, 0(x10)        load the same 4 bytes: forwarded from the store queue, no cache access
```

If the load's bytes are exactly covered by one older store, the value comes straight from the store queue, in a few cycles. If the load only **partly overlaps** the store (a 4-byte store, then an 8-byte load of the same area), simple designs cannot forward: the load waits until the store has drained to the cache, a stall on the order of ten cycles. Code that writes a packet field with one width and reads it back with another is the classic way to hit this.

### 7.3 Memory disambiguation

A load wants to run early, but an older store may not have computed its address yet. Does it conflict?

```text
sw  x5, 0(x10)        address of x10 not known yet (it comes from a slow load)
lw  x6, 0(x11)        might be the same address, might not
```

- **Wait:** the load waits for all older store addresses. Safe, and often slow.
- **Speculate:** assume there is no conflict, run the load now, and check later. If an older store turns out to hit the same address, the load (and everything after it) was wrong: a **memory-ordering violation**, handled like a misprediction (flush from the load and replay).

Real cores use a **memory dependence predictor**: it remembers which loads were hurt by earlier stores and makes only those wait. Predictable conflicts cost a short wait; rare ones cost a replay.

### 7.4 Misses must not block

Cache misses are handled by **miss status holding registers (MSHRs)**, also called line fill buffers: a small table, typically 10 to 16 per core, with one entry per cache line being fetched. The cache keeps serving other accesses while a miss is outstanding (a **non-blocking** cache). The MSHR count is the hardware limit on parallel misses (next section).

---

## 8. Memory-level parallelism and the pointer chase

Consider two loops over the same data, each doing a load that misses in the cache:

```c
// A: independent loads. The addresses are known up front.
for (size_t i = 0; i < n; i++) sum += data[idx[i]];

// B: dependent loads. Each address is the result of the previous load.
while (p) { sum += p->value; p = p->next; }
```

In A, the core sees the next load as soon as the window reaches it and starts its miss without waiting for the previous one. Up to **MLP** misses (memory-level parallelism) are in flight at the same time, so the cost per miss is **latency / MLP**. In B, the next address does not exist until the current load has returned, so every miss pays the **full latency**, one after the other. Nothing in the hardware can overlap them.

**Measured** on a 2.1 GHz Xeon virtual machine, with 128 MB arrays in random order (three runs; your machine will differ):

| Loop | Time per load | Note |
|---|---|---|
| B: pointer chase | about 140 to 170 ns | full latency, and probably a page-table walk for most loads as well (tutorial 15) |
| A: indexed gather | about 14 to 18 ns | many misses overlapped |
| ratio | **about 8 to 10** | the *implied* MLP |

Two limits bound MLP:

1. **MSHRs.** At most 10 to 16 misses can be outstanding.
2. **The window.** Misses overlap only if the core can *see* the later loads: the ROB must hold them. If an iteration is 20 instructions and the ROB has 256 entries, at most 256 / 20 ≈ 12 iterations (so about 12 misses) are in the window.

The pointer chase breaks the first limit differently: the window may hold dozens of nodes, but each node's address depends on the previous one, so the effective MLP is **1**.

**How far ahead can a core run past a miss?** Suppose a load misses to DRAM with a latency of 200 cycles, the core is 4-wide, and the ROB has 256 entries. The core keeps fetching and executing younger instructions, but the missing load sits at the head and nothing can commit. After 256 / 4 = **64 cycles** the ROB is full and the front end stops. The core then idles for the remaining 200 - 64 = **136 cycles**. Only 256 instructions were done in 200 cycles: IPC 1.28 instead of 4. A shorter miss (an L2 hit of about 14 cycles) fits entirely inside the window and costs almost nothing, which is why caches that hit in L2 are "free" for an OoO core and DRAM is not.

**What to do about a chase.**

| Idea | Why it works |
|---|---|
| Use an array or an index-based structure instead of pointers | the next address is computable without waiting for a load; the hardware prefetcher also helps (tutorial 14) |
| Interleave several independent traversals in one loop (`a = a->next; b = b->next; c = c->next; d = d->next`) | four chases give MLP up to 4 |
| Software prefetch, where the address is known early (an array of pointers) | the miss starts before the load needs it |
| Batch operations so that many lookups are in flight together | the same as interleaving |

**Interview phrasing.** "That structure serialises the memory accesses, so I lose memory-level parallelism. I pay the full latency per element instead of latency divided by ten."

---

## 9. How far can this go?

Why not a 20-wide core with a 5000-entry window?

- **The program does not have that much ILP.** Dependences, branch mispredictions (tutorial 10) and cache misses limit the parallelism that exists. Idealised studies find that ordinary integer code has modest ILP unless the window is huge and prediction is nearly perfect.
- **The scheduler does not scale.** Every finished result must be compared with every waiting operand (**wakeup**), and the scheduler must pick ready instructions for the ports every cycle (**select**). The cost grows faster than linearly with the window and the width. This is why the scheduler is much smaller than the ROB.
- **Everything else grows too:** more physical registers with more ports, a bigger rename stage, more bypass wires, a bigger predictor, more power.
- **Returns diminish.** Past a few wide, deep cores per chip, extra transistors are better spent on more cores (tutorial 18), vectors (tutorial 21) or accelerators (tutorials 22 to 24).

---

## 10. The compiler's way to find ILP

The hardware finds ILP at run time. The compiler can arrange the code so that there is more of it, and on in-order cores that is the only way.

### 10.1 Loop unrolling and multiple accumulators

```c
// one chain: each add waits for the previous (latency 4): about 4 cycles per element
for (i = 0; i < n; i++) s += a[i];

// four chains: they overlap, about 1 cycle per element
for (i = 0; i + 4 <= n; i += 4) { s0 += a[i]; s1 += a[i+1]; s2 += a[i+2]; s3 += a[i+3]; }
s = (s0 + s1) + (s2 + s3);
```

The four chains are independent, so a pipelined adder with latency 4 can start one add per cycle. The speed-up is about 4 (until the adder or the loads become the limit). Unrolling also removes loop overhead (counter update and branch). **Floating-point addition is not associative**, so the two loops give slightly different sums; the compiler will not make this change by itself without `-ffast-math`. Fine for sensor readings, not for money.

### 10.2 Software pipelining

Overlap the **iterations** of a loop: start iteration i + 1 before iteration i has finished. Take a loop body of load (latency 2), multiply (latency 4), store. One iteration takes 7 cycles when run alone. Start a new iteration **every cycle** (the *initiation interval*, II = 1):

```text
cycle   1   2   3   4   5   6   7   8   9  10
ld     i0  i1  i2  i3   .   .   .   .   .   .
mul     .   .  i0  i1  i2  i3   .   .   .   .
st      .   .   .   .   .   .  i0  i1  i2  i3
```

Four iterations take 10 cycles instead of 4 x 7 = 28. In general: total = (N - 1) x II + length, so 100 iterations take 99 + 7 = 106 cycles instead of 700. The first and last cycles are the **prologue** and **epilogue**; the cycles in between all do the same thing (the **kernel**). II cannot be smaller than the resource limit (one load unit means one load per cycle) or the **recurrence** limit (a chain that carries a value from iteration to iteration, such as `s += a[i]` with latency 4, means II of at least 4).

### 10.3 Trace scheduling

Branches stop the compiler from moving instructions. **Trace scheduling** picks the most likely path through several branches (the *trace*, from profile data), schedules it as if it were one straight block, and adds **compensation code** on the unlikely exits so that the program still gives correct results there. It trades extra code on the rare path for speed on the common one, and it relies on good profile data.

### 10.4 VLIW and EPIC

A **VLIW** (very long instruction word) machine puts the whole job on the compiler. Each instruction word holds several operations, one for each execution unit, and the compiler guarantees they are independent. The hardware just issues the word: no scheduler, no reorder buffer, no register renaming.

**EPIC** (Explicitly Parallel Instruction Computing, used by the Itanium) is an attempt to make VLIW work for general code: bundles with explicit "these can run together" markers, full predication (tutorial 10, section 9), and speculative loads that the compiler can move above branches.

| | Superscalar OoO | VLIW / EPIC |
|---|---|---|
| Who finds the parallelism | hardware, at run time | compiler, at compile time |
| Hardware | complex (rename, scheduler, ROB) | simple |
| Unpredictable latency (cache misses) | the window finds other work | the whole word stalls; the compiler assumed fixed latencies |
| Code size | normal | larger (unused slots become NOPs) |
| Binary compatibility | a new core runs old binaries well | code depends on the exact widths and latencies of the chip |
| In general-purpose code | wins | the compiler could not find enough ILP across branches and misses |
| Today | all big CPU cores | digital signal processors (for example Qualcomm Hexagon, TI C6000), where loops are regular |

The lesson is not that VLIW is bad. When latencies are fixed and loops are regular, a compiler does the job cheaply. When memory latency varies, dynamic scheduling wins. GPUs take a third route: they do not hunt for ILP in one thread but hide latency with thousands of threads (tutorial 22).

---

## 11. In-order or out-of-order?

| | In-order | Out-of-order |
|---|---|---|
| Area and power | small | large (scheduler, ROB, rename, wide ports) |
| IPC on irregular code | low: stalls on every miss | high: finds other work |
| Compiler matters | a lot | much less |
| Used for | small and efficiency cores, microcontrollers, many network and embedded SoCs | big cores in phones, laptops, servers |

Many "efficiency" cores in phones and PCs are still out of order, only narrower than the performance cores. The difference is one of degree: window, width, ports.

---

## 12. What this means for your code

- **Count dependence chains and misses, not instructions.** A loop with more instructions but several independent chains can beat a shorter loop with one chain.
- **Break serial chains** (several accumulators, independent loop iterations), and know what the compiler may not do (FP reassociation).
- **Avoid pointer chasing in hot paths.** Prefer arrays and indices, interleave independent traversals, or prefetch.
- **A branch that depends on a load that misses resolves late.** Everything fetched behind it is speculative for the whole miss; if the guess is wrong, that work is lost. Data-dependent branches on freshly loaded data are the expensive kind.
- **Do not write a field with one width and read it with another** (packet headers, unions): store forwarding fails.
- **On an in-order core**, keep a load a few instructions away from its use and let the compiler schedule; on an OoO core the hardware reorders within its window.

---

## 13. Mistakes that cost points

1. **"Out of order means the results appear out of order."** Execution is out of order; **commit is in order**. That is what keeps exceptions precise.
2. **"Renaming removes all dependences."** It removes WAR and WAW. True (RAW) dependences remain.
3. **"A wider core is always faster."** Width is bounded by the ILP of the code, by the window, by branch prediction and by memory.
4. **Mixing up the ROB and the scheduler.** The ROB holds *every* in-flight instruction until commit, in order. The scheduler holds only instructions that have not yet been sent to an execution unit.
5. **Freeing a physical register too early.** It is freed when the instruction that replaced its mapping commits, not when its own value is written.
6. **"A store writes the cache when it executes."** It writes after it commits; before that it is only in the store queue.
7. **Forgetting memory disambiguation.** A load cannot simply pass older stores; either it waits, or it speculates and may have to replay.
8. **"The pointer chase is slow only because of cache misses."** It is slow because the misses are **serialised**: an array walk with the same misses overlaps them.
9. **Counting the IPC as the goal.** IPC 4 while spinning on a lock is worthless; useful work per cycle is what counts (tutorial 28).
10. **"Fewer instructions is faster."** Four accumulators execute more instructions and run faster. Stalls are the currency, not instructions.

---

## 14. Questions you will be asked

**What is instruction-level parallelism, and what limits it?** The number of instructions that can execute at the same time. True data dependences (the critical path), branches, and memory latency limit it, and so do the window size and the machine's width.

**What is the difference between in-order and out-of-order execution?** An in-order core stalls at the first instruction that cannot go. An out-of-order core lets independent younger instructions go ahead, then commits in program order.

**What does register renaming do?** It gives every instruction's destination a fresh physical register, removing WAR and WAW dependences. A table (the RAT) holds the newest mapping and a free list supplies new registers.

**What is Tomasulo's algorithm?** Reservation stations hold waiting instructions with operand values or tags; a register status table says who will write each register; a common data bus broadcasts results by tag. It renames through the tags.

**Scoreboard versus Tomasulo?** A scoreboard lets independent instructions pass but keeps names: it stalls issue on WAW and delays writes on WAR. Tomasulo renames, so those stalls disappear, and results are delivered directly to the waiting instructions.

**What is the reorder buffer for?** To commit in program order. It gives precise exceptions, lets the core undo speculation, and bounds how far ahead the core can run.

**How does an out-of-order core recover from a misprediction?** It redirects fetch, flushes the younger instructions from the ROB, scheduler and load/store queues, restores the rename table from a checkpoint and refills the front end. Older instructions are not affected.

**How does a precise exception work?** The exception is recorded in the instruction's ROB entry and taken only when that instruction is at the head. Everything younger is discarded.

**Why do stores wait until commit?** A store is speculative until all older instructions are known to be fine. Other cores must never see a value that might be rolled back.

**What is store-to-load forwarding, and when does it fail?** A load takes its data from an older store in the store queue. It fails when the load only partly overlaps the store (or crosses two stores); then the load waits for the store to reach the cache.

**What limits how far an OoO core can run past a cache miss?** The ROB size (and the scheduler and physical register file), and the number of MSHRs for how many misses can be outstanding.

**Why is a linked list slow?** Locality, and the serialised dependent loads: no memory-level parallelism, so each node pays the full latency instead of latency divided by about ten.

**Walk me through what happens when the CPU executes `a = b + c`.** Fetch and decode, rename (new physical register for `a`), allocate a ROB entry, wait in the scheduler until `b` and `c` are ready, execute on an ALU, write the physical register and wake dependents, commit in order. If `a` is in memory, the store goes into the store queue and drains after commit.

**Why did VLIW not win in general-purpose computing?** The compiler cannot predict cache misses or branches, and binaries depend on the exact hardware. It survives where loops are regular, as in DSPs.

**Why not make the core much wider?** The program's ILP is limited, and the scheduler, register file and bypass network grow faster than linearly in cost and power.

---

## 15. Problems

**How to use this section.** Try each problem before you read the answers. A, B and C are paper problems (25 to 30 minutes each); D is paper plus an optional short program.

### Problem A (paper): dependences and renaming

```text
1  add x1, x2, x3
2  sub x4, x1, x5
3  add x1, x6, x7
4  and x8, x1, x4
5  or  x4, x8, x9
6  add x10, x6, x9
```

Every operation takes 1 cycle.
(a) List every RAW, WAR and WAW dependence (pairs of instructions and the register).
(b) Draw the dataflow graph with only the true dependences. What is the critical path, and the ILP? How many cycles does an out-of-order machine of width 1, 2 and 4 need?
(c) Now suppose the machine cannot rename, so WAR and WAW dependences also force an order (the later instruction must run in a later cycle than the earlier one). Critical path and cycles for width 1, 2 and 4?
(d) Rename the code: architectural `x0` to `x31` start in `p0` to `p31`, the free list starts at `p32`. Write each instruction with physical registers, and say at which commit each old physical register is freed.

*Where:* paper. *How:* sections 1 and 5. A dependence is RAW if the later one reads what the earlier one writes; WAR if the later one writes what the earlier one reads; WAW if both write the same register.

### Problem B (paper): Tomasulo against the scoreboard

Latencies: `fld` 2, `fadd` 2, `fmul` 4. One instruction enters per cycle; one result bus (the older writes first); an instruction starts the cycle after its operands are written and writes the cycle after its last execute cycle.

```text
1  fld   f2, 0(x1)
2  fmul  f4, f2, f0
3  fadd  f6, f4, f2
4  fadd  f2, f8, f9
5  fmul  f10, f2, f8
6  fadd  f12, f10, f9
```

(a) Which instructions have a WAR or WAW dependence, and on what?
(b) Fill in issue, execute and write cycles for **Tomasulo** (and the commit cycle, one per cycle in order).
(c) Fill in issue, read, end and write for the **scoreboard**. Where does it stall, and why?
(d) Give the total of each machine, including the in-order machine of section 4.1 run on this program (its total is 21), and say which chain decides the Tomasulo total.
(e) At the end of cycle 6, write the register status entry of `f2` and the Q fields of instructions 3 and 5.

*Where:* paper. *How:* section 4. In Tomasulo only RAW can delay an instruction; in the scoreboard, a WAW stalls issue and a WAR delays the write.

### Problem C (paper): the ROB, exceptions and the window

(a) Use the Tomasulo trace of section 4.3. Instruction 6 (`fmul f2, f7, f8`) raises an exception when it executes (cycle 10). At what cycle does the core take it? Which instructions are discarded and which are committed by then? What is the state of `f2` in the handler?
(b) Instead, suppose a conditional branch sits between instructions 5 and 6, it was mispredicted, and it resolves (executes) in cycle 20, so 6, 7 and 8 were fetched on the wrong path. Which instructions are flushed, which physical registers are returned to the free list and what does the RAT hold afterwards (use section 5's renaming table), which are untouched, and how much finished work is thrown away?
(c) A core is 6-wide with a 384-entry ROB. A load misses to DRAM with a latency of 250 cycles and nothing else stalls. After how many cycles does the front end stop, how many cycles does the core idle, and what is the average IPC over the miss?
(d) The same core runs a loop with 12 instructions per iteration; each iteration has one independent cache miss, and the core has 16 MSHRs. What is the maximum MLP from the ROB, and from the MSHRs? What cost per miss does that give for a 250-cycle latency?

*Where:* paper. *How:* sections 6, 7.4 and 8.

### Problem D (paper, optional short C): memory-level parallelism

(a) A pointer chase takes 150 ns per node, and an indexed gather over the same data takes 15 ns per element. What MLP is implied? A core has 12 MSHRs: how close is the gather to the limit?
(b) You traverse 4 independent linked lists, one after the other, each of 1 million nodes, at 150 ns per node. Estimate the time. Then traverse all four in a single loop (one node of each per iteration) and estimate the time, assuming the misses overlap perfectly.
(c) The sum loop of section 10.1 runs on a core with a pipelined FP adder of latency 4 and one load per cycle. How many cycles per element with 1 accumulator, 2, 4 and 8? Where does it stop improving?
(d) *(Optional.)* Write the chase and the gather from section 8 in about 20 lines of C: fill `next[]` with a random single cycle (shuffle the numbers 0 to n - 1, then link each to the next in the shuffled order), time 4 million steps of each on 128 MB of data, and compute the ratio.

*Where:* paper for (a) to (c); your machine for (d). *How:* sections 8 and 10.1. The cost per miss is latency / MLP.

---

## Answers

### Problem A

(a)
- RAW: 2 reads `x1` from 1; 4 reads `x1` from 3 and `x4` from 2; 5 reads `x8` from 4.
- WAR: 3 writes `x1`, which 2 reads; 5 writes `x4`, which 4 reads.
- WAW: 3 and 1 both write `x1`; 5 and 2 both write `x4`.
- Instruction 6 depends on nothing earlier (it only reads `x6` and `x9`).

(b) With only the true dependences the levels are: 1 and 3 and 6 in level 1; 2 in level 2; 4 in level 3 (it needs 3 and 2); 5 in level 4. **Critical path 4**, ILP = 6 / 4 = **1.5**. Width 1: 6 cycles. Width 2: {1, 3}, {2, 6}, {4}, {5} = **4** cycles. Width 4: **4** cycles.

(c) Without renaming, 3 must come after 1 (WAW) and after 2 (WAR): level 3. Then 4 (RAW on 3 and 2): level 4; 5 (RAW on 4, WAR on 4, WAW on 2): level 5. **Critical path 5.** Width 1: 6 cycles; width 2: **5**; width 4: **5**. Renaming saves one cycle on this tiny example and would save far more in a loop that reuses the same few registers.

(d)

| # | Renamed | Old mapping freed at the commit of this instruction |
|---|---|---|
| 1 | `add p32, p2, p3` | `p1` |
| 2 | `sub p33, p32, p5` | `p4` |
| 3 | `add p34, p6, p7` | `p32` (instruction 1's result) |
| 4 | `and p35, p34, p33` | `p8` |
| 5 | `or p36, p35, p9` | `p33` (instruction 2's result) |
| 6 | `add p37, p6, p9` | `p10` |

`x1` is split into `p32` (read by 2) and `p34` (read by 4): the WAW and WAR are gone. `p32` is freed when instruction 3 commits, and `p33` when 5 commits.

### Problem B

(a) Instruction 4 writes `f2`: **WAW** with 1 (which writes `f2`) and **WAR** with 2 and 3 (which read `f2`). The others have only RAW dependences: 2 needs 1; 3 needs 2 and 1; 5 needs 4; 6 needs 5.

(b) Tomasulo:

| # | Issue | Execute | Write | Commit |
|---|---|---|---|---|
| 1 | 1 | 2 to 3 | 4 | 5 |
| 2 | 2 | 5 to 8 | 9 | 10 |
| 3 | 3 | 10 to 11 | 12 | 13 |
| 4 | 4 | 5 to 6 | 7 | 14 |
| 5 | 5 | 8 to 11 | **13** | 15 |
| 6 | 6 | 14 to 15 | 16 | 17 |

Instruction 5 could write in cycle 12, but instruction 3 (older) writes then, so 5 writes in 13 (one result bus). Instruction 4 runs at once because it is independent and the name `f2` is renamed by tags.

(c) Scoreboard:

| # | Issue | Read | End | Write |
|---|---|---|---|---|
| 1 | 1 | 2 | 3 | 4 |
| 2 | 2 | 5 | 8 | 9 |
| 3 | 3 | 10 | 11 | 12 |
| 4 | **5** | 6 | 7 | **11** |
| 5 | 6 | 12 | 15 | 16 |
| 6 | 7 | 17 | 18 | 19 |

Instruction 4 cannot **issue** in cycle 4: instruction 1 is still going to write `f2` (WAW), so it issues in cycle 5. It finishes executing in cycle 7 but cannot **write** until instruction 3 has read `f2` in cycle 10 (WAR), so it writes in 11. Instruction 5 needs that new `f2`, so it reads in 12 instead of Tomasulo's 8, and everything after is delayed.

(d) Totals (the cycle in which the last result is written): in-order **21**, scoreboard **19**, Tomasulo **16**. The Tomasulo total is set by the chain 4, 5, 6: 4 writes in cycle 7, 5 runs in 8 to 11, and 6 runs in 14 to 15 and writes in 16. The one-bus conflict at cycle 12 pushed it one cycle later than it would be with two buses.

(e) At the end of cycle 6: `status[f2] = RS4` (instruction 4 took over `f2` when it issued in cycle 4; instruction 1 is long finished). Instruction 3 (`fadd f6, f4, f2`): `Qj` = RS2 (waiting for `f4`), and `Vk` holds the old value of `f2`, which instruction 1 wrote in cycle 4 and instruction 3 captured from the bus. Instruction 5 (`fmul f10, f2, f8`): `Qj` = RS4, `Vk` = the value of `f8`. Instruction 3 uses the old `f2` and instruction 5 the new one, although both say `f2`.

### Problem C

(a) The exception flag is set in cycle 10, but the instruction is not at the head until all older instructions have committed. Instruction 5 commits in cycle 22, so instruction 6 reaches the head in cycle 23 and the core **takes the exception in cycle 23**, 13 cycles after it happened. By then instructions 1 to 5 are **committed**. Instructions 7 and 8 (which already ran) are **discarded**, together with 6 itself. In the handler `f2` has the value of instruction 2 (the last committed write to it; instruction 6 never became architectural) and the saved PC is instruction 6. After the handler returns, 6, 7 and 8 run again.

(b) The branch is resolved in cycle 20. Instructions **6, 7 and 8** (all younger than the branch) are flushed: their ROB entries, scheduler entries and queue entries are cleared. Their physical registers `p37`, `p38` and `p39` return to the free list, and the RAT is restored to its state at the branch, so `f2` maps to `p33` again, `f9` to `p9` and `f10` to `p10` (and `f6` still maps to `p36`, the result of instruction 5). Instructions 1 to 5 and the branch are older and untouched: 1 to 4 have already committed, and 5 is in its last execute cycle. Fetch restarts at the correct target. The flushed instructions had already **finished** (they wrote in cycles 11, 14 and 19), so all of that work is lost: it was done in the shadow of the divide, so it cost no time, but it also wasted energy.

(c) The front end stops after 384 / 6 = **64 cycles**, idles for 250 - 64 = **186 cycles**, and the average IPC over the miss is 384 / 250 = **1.54** (against the width of 6).

(d) ROB limit: 384 / 12 = **32** iterations in the window, so up to 32 misses. MSHR limit: **16**. The MLP is min(32, 16) = **16**, the MSHRs. Cost per miss = 250 / 16 ≈ **15.6 cycles**.

### Problem D

(a) The implied MLP is 150 / 15 = **10**. With 12 MSHRs the gather reaches 10 out of 12 (83% of the limit).

(b) One list after the other: 4 x 1,000,000 x 150 ns = **600 ms**. Four lists in one loop with perfect overlap: each iteration takes about one latency (150 ns) and handles 4 nodes, so 1,000,000 x 150 ns = **150 ms**, four times faster. Both are limited by dependent loads within each list; the lists are independent of each other.

(c) With latency 4 and a pipelined adder: 1 accumulator: **4** cycles per element; 2: **2**; 4: **1**; 8: still **1**. The limit is one load per cycle (the load unit) or the adder's one-per-cycle throughput, whichever is lower; more accumulators beyond 4 do not help. In general, the accumulators needed = latency x throughput of the unit.

(d) Your numbers will differ; expect a ratio between about 5 and about 12 on a typical desktop or server (we measured 8 to 10 on one Xeon VM). If the ratio is near 1, the array is too small and fits in cache; use 128 MB or more.

---

## Cheat sheet

- **ILP:** instructions that can run together. Critical path = the least time on any machine; ILP = instructions / critical path. True dependences (RAW) are real; WAR and WAW are only names; control dependences are predicted.
- **Superscalar:** up to w instructions per cycle, ideal CPI 1/w. **In-order** superscalar stops at the first blocked instruction; **out-of-order** picks any ready one.
- **OoO core:** in-order front end (fetch, decode, rename, allocate in the ROB), out-of-order scheduler and execution, in-order commit.
- **Scoreboard:** issue (stalls on WAW), read operands (waits for RAW), execute, write result (waits for WAR). It lets independent instructions pass but keeps register names.
- **Tomasulo:** reservation stations, tags (Qj, Qk) or values (Vj, Vk), register status, one common data bus. A register is written only if its status still holds the writer's tag. It renames, so WAR and WAW vanish.
- **Renaming (PRF):** RAT + free list + physical register file; every destination gets a new physical register; the old one is freed when the replacing instruction **commits**. It stalls when the free list is empty.
- **ROB:** circular queue in program order; commit from the head when done; precise exceptions (taken at the head); misprediction recovery: flush the younger, restore the RAT (checkpoint), refill.
- **Memory:** stores write the cache only after commit (store queue, then store buffer); loads check the store queue; forwarding works for a full overlap and fails for a partial one; disambiguation: wait, or speculate and replay on a violation; MSHRs bound the parallel misses.
- **MLP:** cost per miss = latency / MLP. MLP is bounded by the MSHRs (10 to 16) and by ROB size / instructions per miss. A pointer chase has MLP 1.
- **Run-ahead past a miss:** ROB size / width cycles, then the core idles for the rest of the latency.
- **Compiler:** unrolling with several accumulators (FP is not associative), software pipelining (total = (N - 1) x II + length), trace scheduling. **VLIW/EPIC:** the compiler packs independent operations; simple hardware, fixed latencies; wins in DSPs, lost in general code.
- **Limits:** the program's ILP, branch accuracy, window size, memory; the scheduler (wakeup and select) and register file scale badly with width.

**Next:** tutorial 12, Inside a Modern Core (the real front end, micro-op cache, issue ports and execution units, the load/store unit, retirement, real numbers for recent Intel, AMD, Apple and Arm cores, instruction latency tables, and how to read a top-down profile). Say "next" when you are ready.
