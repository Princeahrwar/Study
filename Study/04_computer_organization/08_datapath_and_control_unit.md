# CO 08: Datapath and Control Unit

*Part of the [Computer Organization plan](00_plan.md) · Skim tutorial · Time: about 2 hours (1 reading, 1 problems) · You need: [tutorial 03](03_boolean_logic_and_combinational_circuits.md) (muxes, ALU), [tutorial 04](04_sequential_logic_and_state_machines.md) (registers, state machines) and [tutorial 06](06_isa_design.md) (RISC-V instruction formats)*

By the end of this tutorial you can:

- name the parts of a processor datapath and follow one instruction through it;
- say what each control signal does and fill in the control values for an instruction;
- compute the clock period of a single-cycle design from component delays;
- explain how a multi-cycle design reuses hardware and why it isn't automatically faster;
- compare **hardwired** and **microprogrammed** control, and explain horizontal, vertical and nano-programming;
- explain where microcode and micro-ops live in a modern x86 chip.

This is a skim tutorial: it gives you the picture and the numbers. We build a tiny RISC-V processor that runs `add`, `sub`, `and`, `or`, `slt`, `lw`, `sw` and `beq`. The control values, cycle counts and microprogram below were checked by running a small model against a plain interpreter on a test program: all results matched.

---

## 1. The big picture

A processor has two halves.

- The **datapath** holds the data and moves it: the program counter (PC), the register file, the ALU, adders, memories, the immediate generator and the multiplexers (muxes) that choose between sources.
- The **control unit** decides what the datapath does in each cycle. Its outputs are **control signals**: "write the register file", "ALU should subtract", "mux, pick the immediate".

An instruction is just a number that the control unit turns into control signals. Everything in this tutorial is a variation on how and when that happens.

Elements come in two kinds (tutorials 03 and 04):

| Kind | Examples | Behaviour |
|---|---|---|
| **State** (remembers) | PC, register file, instruction memory, data memory | changes only at a clock edge |
| **Combinational** (computes) | ALU, adders, immediate generator, muxes, control logic | output depends only on current inputs |

Recall the RISC-V formats from tutorial 06. The fields sit in the same place in every format, which is what makes the datapath simple:

```text
 R-type   funct7 | rs2 | rs1 | funct3 | rd      | opcode     add, sub, and, or, slt
 I-type   imm[11:0]   | rs1 | funct3 | rd      | opcode     lw, addi
 S-type   imm[11:5] | rs2 | rs1 | funct3 | imm[4:0] | opcode  sw
 B-type   imm bits  | rs2 | rs1 | funct3 | imm bits | opcode  beq
```

The opcodes we need: R-type `0110011`, `lw` `0000011`, `sw` `0100011`, `beq` `1100011` (and `addi` `0010011`, which you will add in Problem B).

---

## 2. The single-cycle datapath

In a **single-cycle** design every instruction runs from start to finish in one clock cycle. The five jobs, left to right:

```text
        +-------------+    +---------------+    +--------+    +-----------+    +------------+
 PC --->| Instruction |--->| Register file |--->|  ALU   |--->|   Data    |--->| Write-back |--> register file
        | memory      |    | read rs1, rs2 |    |        |    |   memory  |    | mux        |    (rd)
        +-------------+    +---------------+    +--------+    +-----------+    +------------+
                                                   ^   ^                              ^
                                       ALU control |   | ALUSrc: B or immediate       | MemtoReg: ALU result or memory data
   Next PC = (Branch AND Zero) ? PC + imm : PC + 4
```

What each instruction does with those pieces (the path through the diagram):

| Step | `add` (R-type) | `lw` | `sw` | `beq` |
|---|---|---|---|---|
| 1. Fetch | read `instr` at PC | same | same | same |
| 2. Read registers | read rs1, rs2 | read rs1 | read rs1, rs2 | read rs1, rs2 |
| 3. ALU | rs1 `op` rs2 | rs1 + immediate (address) | rs1 + immediate (address) | rs1 - rs2 (is it zero?) |
| 4. Memory | none | read | write rs2 | none |
| 5. Write back | result to rd | memory data to rd | none | none |
| Next PC | PC + 4 | PC + 4 | PC + 4 | PC + imm if equal, else PC + 4 |

Two small pieces make this work:

- The **immediate generator** takes the 32-bit instruction and builds the sign-extended immediate for whichever format it is (I, S or B). It is a bundle of wires and a little logic.
- The **ALU** needs to compute an address for `lw`/`sw` and a comparison for `beq`. The **Zero** output (result == 0) is how `beq` decides.

The design also needs **two adders besides the ALU** (PC + 4, and PC + imm) and **separate instruction and data memories**, because both are used in the same cycle.

---

## 3. Control signals

| Signal | When 1 (or its value) |
|---|---|
| `RegWrite` | the register file writes `rd` at the end of the cycle |
| `ALUSrc` | ALU's second input is the immediate (0 means register rs2) |
| `MemRead` | data memory is read |
| `MemWrite` | data memory is written |
| `MemtoReg` | the value written to `rd` comes from memory (0 means from the ALU) |
| `Branch` | this is a branch; take it if the ALU's Zero is 1 |
| `ALUOp` (2 bits) | `00` always add, `01` always subtract, `10` look at the funct fields |

The **main decoder** turns the opcode into these values. X means "don't care" (tutorial 03).

| Instruction | ALUSrc | MemtoReg | RegWrite | MemRead | MemWrite | Branch | ALUOp |
|---|---|---|---|---|---|---|---|
| R-type | 0 | 0 | 1 | 0 | 0 | 0 | 10 |
| `lw` | 1 | 1 | 1 | 1 | 0 | 0 | 00 |
| `sw` | 1 | X | 0 | 0 | 1 | 0 | 00 |
| `beq` | 0 | X | 0 | 0 | 0 | 1 | 01 |

A second small decoder, the **ALU control**, picks the actual ALU operation:

| ALUOp | funct7 bit 5 | funct3 | ALU operation |
|---|---|---|---|
| `00` | X | X | add (addresses for `lw`, `sw`) |
| `01` | X | X | subtract (`beq`) |
| `10` | 0 | `000` | add |
| `10` | 1 | `000` | subtract |
| `10` | 0 | `111` | and |
| `10` | 0 | `110` | or |
| `10` | 0 | `010` | set-less-than (`slt`) |

`slt` reuses the subtractor: the ALU computes a − b and puts the "less than" bit (N xor V, tutorial 03) in bit 0 of the result.

### Following `lw x6, 16(x2)`

The instruction word is `0x01012303` (from tutorial 06). Its fields: opcode `0000011` (load), rd = 6, funct3 = 010, rs1 = 2, immediate = 16. Suppose `x2 = 0x1000` and memory at `0x1010` holds `0xABCD`.

1. **Fetch:** the instruction is read from memory at PC.
2. **Control:** the decoder sees opcode `0000011` and sets ALUSrc = 1, MemtoReg = 1, RegWrite = 1, MemRead = 1, MemWrite = 0, Branch = 0, ALUOp = 00.
3. **Register read:** `x2` is read, giving `0x1000`.
4. **ALU:** the immediate generator produces 16; ALUSrc picks it; ALUOp = 00 means add: `0x1000 + 16 = 0x1010`.
5. **Memory:** MemRead = 1, so `0xABCD` comes out of address `0x1010`.
6. **Write back:** MemtoReg = 1 picks the memory data and RegWrite = 1 writes `0xABCD` into `x6` at the clock edge.
7. **Next PC:** Branch = 0, so PC becomes PC + 4.

### Other instructions

New instructions mostly add a mux input and a decoder row. `addi` is the `lw` path without the memory (ALUSrc = 1, MemtoReg = 0, RegWrite = 1). `lui` needs one mux input that passes the immediate straight to the write-back. `jal` needs an input that writes PC + 4 into `rd` and another that loads the PC from the target adder. Exceptions and interrupts add a path that saves the PC and loads a handler address; they are easiest to understand in a pipeline (tutorial 09).

---

## 4. The critical path and the clock

The clock period of a single-cycle design must cover the **slowest instruction**, because the same clock drives all of them. Suppose these component delays (our own illustrative numbers; adders, muxes and wires are ignored):

| Instruction memory | Register read | ALU | Data memory | Register write |
|---|---|---|---|---|
| 200 ps | 100 ps | 200 ps | 200 ps | 100 ps |

The time each instruction type needs is the sum of the units it passes through:

| Instruction | Path | Time |
|---|---|---|
| R-type | I-mem + reg read + ALU + reg write | 600 ps |
| `lw` | I-mem + reg read + ALU + D-mem + reg write | **800 ps** |
| `sw` | I-mem + reg read + ALU + D-mem | 700 ps |
| `beq` | I-mem + reg read + ALU | 500 ps |

`lw` is the **critical path**, so the clock period is **800 ps** (1.25 GHz) and every instruction takes 800 ps, even a `beq` that only needs 500. With a typical mix of 52% R-type, 25% `lw`, 10% `sw`, 13% `beq`, a machine that could somehow use each instruction's own time would average `0.52 x 600 + 0.25 x 800 + 0.10 x 700 + 0.13 x 500 = 647 ps`, about 19% less time (a 1.24x speedup). That gap, and the duplicated hardware, are the weaknesses of the single-cycle design.

---

## 5. The multi-cycle datapath

The **multi-cycle** idea: split each instruction into steps, let each step take one short cycle, and let simple instructions finish in fewer cycles. Because steps no longer happen at the same time, one ALU and one memory can serve all of them. New **registers** hold values between steps: `IR` (the instruction), `A` and `B` (the register file outputs), `ALUOut` and `MDR` (memory data), plus a copy `OldPC`.

The steps for each instruction class in our design (register transfers):

| Cycle | All instructions | R-type | `lw` | `sw` | `beq` |
|---|---|---|---|---|---|
| 1 Fetch | `IR <- Mem[PC]`, `OldPC <- PC`, `PC <- PC+4` | | | | |
| 2 Decode | `A <- Reg[rs1]`, `B <- Reg[rs2]`, `ALUOut <- OldPC + imm_B` | | | | |
| 3 Execute | | `ALUOut <- A op B` | `ALUOut <- A + imm_I` | `ALUOut <- A + imm_S` | `if A-B == 0, PC <- ALUOut` |
| 4 Memory / write back | | `Reg[rd] <- ALUOut` | `MDR <- Mem[ALUOut]` | `Mem[ALUOut] <- B` | (done) |
| 5 Write back | | | `Reg[rd] <- MDR` | | |
| **Cycles** | | **4** | **5** | **4** | **3** |

Decode computes the branch target for every instruction because the ALU is free in that cycle and it doesn't know yet what the instruction is; a non-branch simply ignores the result. The control unit is now a **sequence** of states instead of a single lookup:

```text
Fetch --> Decode --> R-type:  Execute --> Write back ------------------+
                 |-> lw:      Address --> Memory read --> Write back --+--> back to Fetch
                 |-> sw:      Address --> Memory write ----------------+
                 '-> beq:     Compare and branch ----------------------+
```

The longest step (a memory access or the ALU) sets the clock: **200 ps**. The result for the same mix as before:

| Design | Clock | Cycles per instruction (average) | Time per instruction |
|---|---|---|---|
| Single-cycle | 800 ps | 1 | **800 ps** |
| "Ideal" instruction-sized cycle | varies | 1 | 647 ps |
| Multi-cycle | 200 ps | `0.52x4 + 0.25x5 + 0.10x4 + 0.13x3 = 4.12` | **824 ps** |
| Pipelined (tutorial 09) | about 220 ps (200 plus register overhead) | about 1.3 with typical stalls | about 280 ps |

**The multi-cycle design is not faster here.** A load still needs 5 x 200 = 1000 ps instead of 800, because the steps are unbalanced (register read and write are only 100 ps but get a full 200 ps cycle) and the average CPI is above 4. Its real benefits are **hardware reuse** (one ALU, one memory) and that it is the stepping stone to pipelining, where the same short steps overlap and CPI approaches 1. This is why the iron law (tutorial 01) needs all three terms: a shorter cycle only helps if the cycle count doesn't grow as much.

---

## 6. Hardwired control

**Hardwired control** builds the control unit from gates, exactly the tutorial 03 and 04 toolbox.

- In the **single-cycle** design the control unit is purely combinational: a truth table from the opcode (and funct bits) to the control signals. Reading the main decoder table as logic, with `R`, `lw`, `sw`, `beq` standing for "the opcode is that of ...":

```text
RegWrite = R + lw                 MemRead  = lw
MemWrite = sw                     ALUSrc   = lw + sw
MemtoReg = lw                     Branch   = beq
```

  Each equation is a few gates on the opcode bits, typically built as a small **PLA** (a grid of AND gates feeding OR gates) or a ROM.
- In the **multi-cycle** design the control unit is a **finite state machine** (tutorial 04): a state register holds "Fetch", "Decode", "R-execute", and so on; next-state logic follows the state diagram above (the opcode picks the branch after Decode); output logic turns the state into control signals. It is a Moore machine whose outputs depend on the state alone.

Hardwired control is **fast** (a few gate delays) and compact for a small instruction set. It is also **hard to change**: fixing a bug or adding an instruction means redesigning logic.

### A trap: `addi` and the ALU control

Add the instruction `addi` (I-type, opcode `0010011`) and reuse ALUOp = `10` so the funct3 field picks the operation. There is a catch. In an R-type instruction, bit 30 (the funct7 bit that distinguishes `sub` from `add`) is part of the opcode extension; in `addi` bit 30 is just a bit of the **immediate**. `addi x13, x13, -1` encodes as `0xFFF68693`, whose bit 30 is 1 because -1 is all ones. A decoder that looks at bit 30 whenever ALUOp = `10` turns it into `sub x13, x13, -1`. I ran this: with `x13 = 5`, the naive decoder gives 6 instead of 4. The fix: only treat bit 30 as "subtract" when the instruction is an R-type (opcode bit 5 is 1).

---

## 7. Microprogrammed control

**Microprogrammed control** replaces the gates with a small memory. Each control state becomes a row, called a **microinstruction**, in a **control store** (a ROM). A **micro-program counter** (µPC) steps through the rows, and each row contains the control signals for one cycle plus a field that says what comes next.

```text
 opcode ---> [ dispatch ROM ] --+
                                v
 [ µPC ] ---> [ control store ] ---> control signals --> datapath
    ^              |  (next: seq / dispatch / fetch / jump)
    +--------------+
```

For our multi-cycle machine, the control word has 14 signals. The bit order is `MemRead MemWrite IorD IRWrite PCWrite PCSource PCWriteCond ALUSrcA ALUSrcB(2) ALUOp(2) RegWrite MemtoReg` (`IorD` = 1 means the memory address comes from `ALUOut`, 0 from the PC; `ALUSrcA` = 1 means the ALU's first input is `OldPC`, 0 means `A`; `ALUSrcB` = 00 is register B, 01 the I-immediate, 10 the S-immediate, 11 the B-immediate; `PCSource` = 1 takes `ALUOut` as the new PC; `PCWriteCond` writes the PC only if Zero). The **microprogram**:

| µaddr | Step | Micro-operation | Control word | Next |
|---|---|---|---|---|
| 0 | fetch | `IR <- Mem[PC]; OldPC <- PC; PC <- PC+4` | `10011000000000` | next row |
| 1 | decode | `A, B <- regs; ALUOut <- OldPC + imm_B` | `00000001110000` | **dispatch** on opcode |
| 2 | R-execute | `ALUOut <- A op B` | `00000000001000` | next row |
| 3 | R write back | `Reg[rd] <- ALUOut` | `00000000000010` | fetch (row 0) |
| 4 | lw address | `ALUOut <- A + imm_I` | `00000000010000` | next row |
| 5 | lw memory | `MDR <- Mem[ALUOut]` | `10100000000000` | next row |
| 6 | lw write back | `Reg[rd] <- MDR` | `00000000000011` | fetch |
| 7 | sw address | `ALUOut <- A + imm_S` | `00000000100000` | next row |
| 8 | sw memory | `Mem[ALUOut] <- B` | `01100000000000` | fetch |
| 9 | beq | `if A-B == 0, PC <- ALUOut` | `00000110000100` | fetch |

The dispatch ROM maps an opcode to the first row of its routine: R-type to row 2, `lw` to 4, `sw` to 7, `beq` to 9. A `lw` therefore runs rows 0, 1, 4, 5, 6: five cycles, matching the earlier table. Running this microprogram through the model gives the same registers and memory as the plain interpreter.

**Adding an instruction is adding rows.** To support `addi`, add one row and one dispatch entry; no gate is redesigned. That flexibility is the main appeal: complex instruction sets (which need long sequences) were practical to build this way, and bugs could be fixed by changing the ROM.

### Horizontal, vertical and nano

- **Horizontal** microcode has one bit per control signal, as above (14 bits + 2 for sequencing = 16 bits per row, 160 bits for 10 rows). It is **fast** (no decoding, many things can happen at once) but the rows are **wide**.
- **Vertical** microcode encodes the signals into small fields (say, a 2-bit memory-operation field instead of separate read, write and address signals) and uses decoders to expand them. Rows are **narrower**, but the decode adds delay and the encoding limits what can happen in parallel. Our 10 rows have 10 different control words, so the extreme case is a 4-bit code (ceil(log2 10)) decoded to 14 signals: 4 + 2 = 6 bits per row, 60 bits for 10 rows.
- **Nano-programming** uses **two levels**: the micro-store holds short indices, and each index points into a **nano-store** that holds the actual wide control words. It pays off when many micro-rows repeat the same control word. For example, a design with 2000 rows and 100-bit control words, where only 300 distinct words occur: a single level needs 2000 x 100 = 200,000 bits; two levels need 2000 x 9 (index bits) + 300 x 100 = 48,000 bits, 76% less. The price is an extra lookup in every step. The Motorola 68000 is a commonly cited example of a processor built this way.

A **writable control store** lets the microprogram be patched after the chip is built, which leads to the next point.

---

## 8. Hardwired versus microprogrammed, and x86 today

| | Hardwired | Microprogrammed |
|---|---|---|
| Speed | fastest (gates) | slower (a memory read per step) |
| Flexibility | fixed once built | change the ROM (or patch it) |
| Complex instructions | hard to build | natural: a longer microprogram |
| Design effort | high for large instruction sets | lower and more regular |
| Typical use | RISC cores (RISC-V, most Arm) | CISC machines historically; rare complex instructions today |

Modern high-performance cores **mix the two**. On x86 (tutorial 06's micro-ops):

- Most instructions are decoded by **hardwired decoders** into one or a few **micro-ops** (µops), simple RISC-like operations that the core executes, often out of order (tutorial 11). `add [mem], eax` becomes a load, an add and a store.
- Instructions that need more than a few µops (`rep movsb`, `cpuid`, some complex arithmetic and system instructions) are handled by a **microcode ROM**: the decoder jumps into it and a sequencer emits the µops, exactly the µPC-and-control-store idea in section 7.
- The microcode has a **patch** mechanism: the firmware or the operating system can load a microcode update at boot. This is how many CPU errata and some security mitigations (the speculative-execution fixes of tutorial 30) were delivered without new silicon. On Linux, `grep microcode /proc/cpuinfo` shows the revision that is loaded (a virtual machine may show a dummy value, as mine does: `0x1`).

Don't confuse the two words: **micro-ops** are the internal instructions every x86 instruction becomes; **microcode** is the ROM sequence that produces µops for the few instructions too complex for the hardwired decoders.

---

## 9. Mistakes that cost points

1. **Saying the multi-cycle design is faster than the single-cycle one.** It has a shorter clock and a higher CPI; whether it wins depends on the numbers (here it doesn't). Its value is hardware reuse and as a step to pipelining.
2. **Forgetting the single-cycle clock is set by the slowest instruction** (usually `lw`), not the average.
3. **Mixing up `MemtoReg` and `RegWrite`**: `RegWrite` decides *whether* a register is written, `MemtoReg` decides *what* is written.
4. **Letting the ALU control read bit 30 for `addi`** and turning `addi x, x, -1` into a subtraction (section 6).
5. **Calling micro-ops "microcode".** µops are the internal instructions; microcode is a ROM that generates them for complex instructions.
6. **Forgetting the sequencing field** of a microinstruction: a row holds both control signals and "what next".

---

## 10. Questions you will be asked

**Walk me through `lw` in a single-cycle datapath.** Fetch the instruction; the main decoder sets ALUSrc = 1, MemRead = 1, MemtoReg = 1, RegWrite = 1; read rs1; the ALU adds rs1 and the sign-extended immediate; data memory returns the word; the write-back mux picks it; the register file writes rd at the clock edge; PC becomes PC + 4.

**Why is a single-cycle design slow?** The clock must fit the slowest instruction (a load), so every instruction takes that long, and it needs duplicated hardware (separate memories, extra adders).

**What does a multi-cycle design buy you?** A shorter clock, instructions that take only the cycles they need, and shared hardware (one ALU, one memory). It also leads naturally to pipelining.

**Hardwired versus microprogrammed control?** Hardwired is gates: fast, hard to change. Microprogrammed is a control store plus a sequencer: slower, flexible, easy to extend and patch.

**Horizontal versus vertical microcode?** Horizontal has one bit per signal: wide and fast. Vertical encodes signals into fields: narrower but needs decoding and limits parallelism.

**What is nano-programming?** Two-level microcode: a micro-store of indices into a nano-store of unique control words. It saves space when control words repeat.

**What is microcode in a modern x86 processor?** A ROM sequence that produces micro-ops for complex instructions the hardwired decoders don't handle. It can be patched by a firmware or OS update.

**What is the critical path?** The longest delay between state elements; it sets the minimum clock period.

---

## 11. Problems

**How to use this section.** Try each problem before looking at the answers at the bottom. All four are paper problems (about 20 minutes each).

### Problem A (paper): clock and cycles

Component delays: instruction memory 250 ps, register read 125 ps, ALU 150 ps, data memory 250 ps, register write 125 ps. The mix is 40% R-type, 30% `lw`, 10% `sw`, 20% `beq`.
(a) The path time of each instruction type, and the single-cycle clock period and frequency.
(b) For a multi-cycle design with a clock equal to the slowest step (4, 5, 4 and 3 cycles for R-type, `lw`, `sw`, `beq`): the average CPI and the time per instruction. Which design is faster?
(c) How much does the single-cycle clock improve if the ALU becomes twice as fast? If the data memory does?

*Where:* paper (or a calculator). *How:* section 4's method: add the units along each path; the clock is the longest path; multi-cycle time is CPI x slowest step.

### Problem B (paper): control signals

(a) For `lw x6, 16(x2)` with `x2 = 0x2000` and `mem[0x2010] = 0x55`, give the ALU result, the memory output and what is written where.
(b) Add `addi` to the main decoder table: what values do the seven signals take? Why must the ALU control look at opcode bit 5 as well as the funct bits?
(c) For `sw x7, 8(x1)`, which signals are 1 and what does the ALU compute?

*Where:* paper. *How:* sections 2, 3 and 6; for (b) start from the `lw` row and change only what differs.

### Problem C (paper): multi-cycle

(a) Write the register transfers and the control-word 1s of each cycle of `sw x7, 8(x1)` (use the microprogram table).
(b) A program has 30 R-type, 20 `lw`, 10 `sw` and 15 `beq` instructions. How many cycles does the multi-cycle machine need, and how long does it take at 200 ps per cycle? How long would a single-cycle machine (800 ps) take?
(c) Why can't the multi-cycle machine fetch the next instruction while a `lw` is reading memory?

*Where:* paper. *How:* (a) rows 0, 1, 7 and 8; (b) cycles per class times counts; (c) look at what is shared.

### Problem D (paper): microprogram and size

(a) Describe what is needed to add `addi` to the microprogram of section 7: how many new rows, their control word fields, and the dispatch entry.
(b) A microprogrammed design has 1500 rows and 80-bit control words, of which only 150 are distinct. Compare the control-store size of a single level with a nano-programmed two-level design.
(c) What does `grep microcode /proc/cpuinfo` show, and what is it a revision of?

*Where:* paper; (c) on any Linux shell. *How:* (a) reuse row 3; (b) the arithmetic of section 7; (c) section 8.

---

## Answers

Try before reading.

### Problem A

(a) R-type: 250 + 125 + 150 + 125 = **650 ps**. `lw`: 250 + 125 + 150 + 250 + 125 = **900 ps**. `sw`: 250 + 125 + 150 + 250 = **775 ps**. `beq`: 250 + 125 + 150 = **525 ps**. The clock is the longest path, `lw`: **900 ps**, or about 1.11 GHz.

(b) The slowest step is 250 ps (a memory access), so the cycle is 250 ps. CPI = 0.4 x 4 + 0.3 x 5 + 0.1 x 4 + 0.2 x 3 = 1.6 + 1.5 + 0.4 + 0.6 = **4.1**. Time per instruction = 4.1 x 250 = **1025 ps**. The single-cycle design (900 ps) is faster.

(c) ALU twice as fast: the `lw` path becomes 900 - 75 = **825 ps** (it is still the longest). Data memory twice as fast: 900 - 125 = **775 ps**. A memory is a bigger part of the critical path than the ALU, so speeding it up helps more.

### Problem B

(a) The instruction is a load with ALUSrc = 1, ALUOp = 00. The ALU adds `0x2000 + 16 = 0x2010`. The data memory outputs `0x55`. MemtoReg = 1 selects it and RegWrite = 1 writes `0x55` into `x6`.

(b) `addi`: ALUSrc = 1 (immediate), MemtoReg = 0 (result comes from the ALU), RegWrite = 1, MemRead = 0, MemWrite = 0, Branch = 0, ALUOp = 10 (let funct3 choose; `000` means add). The ALU control must also check opcode bit 5 (1 for R-type, 0 for I-type): in an I-type instruction, bit 30 belongs to the immediate and may be 1 (for example for a negative immediate), and without the check an `addi` with a negative immediate would be decoded as `sub`.

(c) `sw`: ALUSrc = 1, MemWrite = 1, ALUOp = 00; the others are 0 (RegWrite = 0, MemRead = 0, Branch = 0; MemtoReg doesn't matter). The ALU computes `x1 + 8`, the address; the data written is `x7`.

### Problem C

(a) Cycle 1 (row 0): `IR <- Mem[PC]; OldPC <- PC; PC <- PC+4`. Signals 1: MemRead, IRWrite, PCWrite. Cycle 2 (row 1): `A, B <- registers; ALUOut <- OldPC + imm_B`. Signals 1: ALUSrcA, ALUSrcB (immediate B, value 3). Cycle 3 (row 7): `ALUOut <- A + imm_S`. Signals: ALUSrcB (value 2); ALUOp = 00. Cycle 4 (row 8): `Mem[ALUOut] <- B`. Signals 1: MemWrite, IorD. Four cycles.

(b) Cycles: 30 x 4 + 20 x 5 + 10 x 4 + 15 x 3 = 120 + 100 + 40 + 45 = **305 cycles**, times 200 ps = **61,000 ps = 61 ns**. A single-cycle machine runs 75 instructions x 800 ps = **60,000 ps = 60 ns**. About the same: the multi-cycle design is slightly slower here.

(c) There is only **one memory**, and it is busy with the `lw` data read in the memory cycle, and the **ALU and PC logic are shared** too. The fetch needs the memory (and the PC update), so it must wait. Letting fetch overlap with other steps needs separate hardware, which is what the pipeline provides (tutorial 09).

### Problem D

(a) One new row: `addi-execute`, with `ALUSrcA = 0`, `ALUSrcB = immediate I`, `ALUOp = 10` (funct3 chooses the operation; the ALU control must apply the opcode-bit-5 rule from section 6), next = **jump to row 3** (R write back, `Reg[rd] <- ALUOut`), so no write-back row is needed. A jump needs a target address, so either the sequencing field grows an address (4 bits for 11 rows, widening every row) or you simply add a second copy of the write-back row that ends in "fetch" (two new rows, no new field). One new **dispatch entry**: opcode `0010011` goes to the new row. Cycles: 4, the same as R-type. (Checked with the model: a test program using `addi` gives the right result, with 4 cycles for the `addi`.)

(b) Single level: 1500 x 80 = **120,000 bits**. Two-level: the index needs ceil(log2 150) = 8 bits, so micro-store = 1500 x 8 = 12,000 bits, nano-store = 150 x 80 = 12,000 bits, total **24,000 bits**: five times smaller, or 80% saved.

(c) It shows the **microcode revision** loaded into the CPU, the version of the microcode patch that firmware or the OS applied at boot (inside a virtual machine it may be a dummy value).

---

## Cheat sheet

- **Datapath** = state (PC, registers, memories) + combinational parts (ALU, adders, immediate generator, muxes). **Control** = signals that tell them what to do.
- **Single-cycle:** one instruction per cycle; clock = slowest path (usually `lw`: I-mem + reg read + ALU + D-mem + reg write); duplicated hardware.
- **Control signals:** `RegWrite`, `ALUSrc`, `MemRead`, `MemWrite`, `MemtoReg`, `Branch`, `ALUOp` (`00` add, `01` subtract, `10` funct). Next PC = PC + imm if Branch AND Zero, else PC + 4.
- **ALU control trap:** for I-type arithmetic, bit 30 is part of the immediate; only R-type uses it to choose `sub`.
- **Multi-cycle:** steps Fetch, Decode, Execute, Memory, Write back; extra registers (`IR`, `A`, `B`, `ALUOut`, `MDR`); one ALU and one memory; cycles 4, 5, 4, 3 for R-type, `lw`, `sw`, `beq`; clock = slowest step. Not automatically faster (our numbers: 824 ps against 800 ps).
- **Hardwired control:** gates (PLA) for single-cycle, an FSM for multi-cycle: fast, rigid.
- **Microprogrammed control:** control store + µPC + dispatch ROM; one row per control state; horizontal (one bit per signal), vertical (encoded fields), nano (two-level) to save bits; easy to extend and patch.
- **x86:** hardwired decoders make micro-ops; a microcode ROM handles the complex instructions and can be patched at boot.

**Next:** tutorial 09, Pipelining and Hazards (the 5-stage pipeline, structural, data and control hazards, forwarding and stalls, and CPI with hazards). Say "next" when you are ready.
