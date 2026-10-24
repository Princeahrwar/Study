# CO 06: ISA Design

*Part of the [Computer Organization plan](00_plan.md) · Core tutorial · Time: about 3 hours (1.5 reading, 1.5 problems) · You need: [tutorial 01](01_how_a_computer_works.md) (instructions, fetch-decode-execute) and [tutorial 02](02_data_representation.md) (two's complement, byte order, alignment)*

By the end of this tutorial you can:

- say what an ISA defines, and what it leaves to the hardware designer;
- compare the five ISA classes (stack, accumulator, register-memory, memory-memory, load/store) and the 0-, 1-, 2- and 3-address instruction formats;
- explain fixed- versus variable-length encoding, and encode and decode a RISC-V instruction by hand;
- name every common **addressing mode** and find it in real x86-64, AArch64 and RISC-V compiler output;
- explain **RISC versus CISC** accurately, including micro-ops, and why the slogans mislead;
- read basic RISC-V assembly, the teaching ISA used for the pipeline tutorials.

Every assembly listing below is real output from `clang 18` or `gcc 13`, except where marked as schematic. Your compiler may differ by a few instructions.

---

## 1. What an ISA is

The **instruction set architecture (ISA)** is the contract between software and hardware: everything a programmer (or compiler) must know to write a correct program for a processor, and nothing about how the processor is built. It defines:

- the **instructions** and their binary **encoding**;
- the **registers**, their sizes and special roles;
- how memory is addressed: **addressing modes**, byte order, alignment rules, the memory-ordering model (tutorial 20);
- the data types the instructions work on (integers, floats, vectors);
- **privilege levels, exceptions and interrupts**: what happens on a fault or a system call (tutorial 26).

How the instructions are *carried out*, whether the core is pipelined, how many it runs at once, how big the caches are, is the **microarchitecture** (tutorial 01 section on ISA versus implementation). Many different microarchitectures implement one ISA: an Intel Atom and a server Xeon both run the same x86-64 programs.

Three families matter for you:

| ISA | Where | Notes |
|---|---|---|
| **x86-64** | PCs, servers | Intel and AMD; variable-length instructions, extended many times while staying compatible |
| **AArch64** (ARM64) | phones, Apple silicon, Wi-Fi/network SoCs, cloud CPUs | fixed 32-bit instructions; licensed by Arm |
| **RISC-V** | embedded, accelerators, teaching | open standard, small clean base plus optional extensions |

(Von Neumann versus Harvard from tutorial 01 is a memory-organization choice; most CPUs today are von Neumann in main memory and "modified Harvard" with separate L1 instruction and data caches.)

---

## 2. Where do the operands live? Five ISA classes

The first design choice: where do an instruction's inputs come from, and where does the result go? Take `c = a + b`.

| Class | Code for `c = a + b` | Instructions | Data-memory accesses | Seen in |
|---|---|---|---|---|
| **Stack** (0-address) | `push a` `push b` `add` `pop c` | 4 | 3 | JVM bytecode, WebAssembly, Python bytecode |
| **Accumulator** (1-address) | `load a` `add b` `store c` | 3 | 3 | early computers, tiny microcontrollers |
| **Register-memory** | `mov eax,[a]` `add eax,[b]` `mov [c],eax` | 3 | 3 | x86 |
| **Memory-memory** | `add c, a, b` | 1 | 3 | VAX (historical) |
| **Load/store** (register-register) | `lw t0,a` `lw t1,b` `add t2,t0,t1` `sw t2,c` | 4 | 3 | AArch64, RISC-V, MIPS |

(The load/store row shows names instead of addresses for readability; real code first puts the address in a register.)

The memory traffic is the same, but the instruction counts differ. The **stack** machine is compact (`add` names no operands) and easy to generate code for, but every operation goes through the top of the stack, so there is little to run in parallel. **Memory-memory** looks efficient but every instruction does up to three memory accesses, which makes pipelining a nightmare; it died out.

**Load/store** won. Only two kinds of instruction touch memory (loads and stores); all arithmetic works on registers. That gives:

1. every instruction does one simple thing, so a pipeline (tutorial 09) can handle them uniformly;
2. fixed-size, easy-to-decode instructions;
3. values stay in registers across statements: a value used five times is loaded once;
4. memory faults (page faults, alignment) can happen only in loads and stores, which simplifies exceptions.

x86 is **register-memory**: ALU instructions may take one memory operand (`add eax, [rbx]`). In practice x86 hardware splits such instructions into load/store-style micro-ops internally (section 8).

**Registers.** x86-64 has 16 general-purpose registers (32-bit x86 had 8, which forced compilers to spill values to the stack constantly). AArch64 has 31 (plus a zero register and the stack pointer). RISC-V has 32, of which **x0 is hardwired to zero**, a neat trick: reading x0 gives 0 and writing it does nothing, so `mv`, `neg`, `nop`, compare-with-zero and "discard the result" all come free without extra opcodes.

---

## 3. How many addresses does an instruction name?

An instruction has an **opcode** (what to do) and **operand fields** (where the data is). The number of operands it names gives the format. Take `X = (A + B) * (C - D)` with all variables in memory:

```text
3-address:  ADD T1, A, B         dest = src1 op src2
            SUB T2, C, D
            MUL X, T1, T2                                   3 instructions

2-address:  MOV T1, A            dest = dest op src
            ADD T1, B
            MOV T2, C
            SUB T2, D
            MUL T1, T2
            MOV X, T1                                       6 instructions

1-address:  LOAD A               the accumulator is implicit
            ADD B
            STORE T
            LOAD C
            SUB D
            MUL T
            STORE X                                         7 instructions

0-address:  PUSH A               operands are on the stack
            PUSH B
            ADD
            PUSH C
            PUSH D
            SUB
            MUL
            POP X                                           8 instructions
```

More operand fields per instruction means fewer instructions but longer encodings (each field costs bits). x86 is **2-address** (`add eax, ebx` means `eax = eax + ebx`; the destination is also a source and gets overwritten). AArch64 and RISC-V are **3-address** (`add x0, x1, x2`), which keeps both inputs alive. Modern x86 vector instructions (AVX) are 3-operand as well.

Don't confuse the two ideas: *class* is where operands may live (registers or memory), *address count* is how many the instruction names.

---

## 4. Encoding instructions

The processor reads instructions as bits. The encoding decides how many bits an instruction takes, what the fields are, and how fast it can be decoded.

### Fixed versus variable length

| | Fixed length (AArch64, RISC-V base, MIPS) | Variable length (x86-64, 1 to 15 bytes) |
|---|---|---|
| Finding the next instruction | `PC + 4`; trivial | must decode this one to learn its length |
| Decoding several at once | easy, they are at known places | hard; real x86 cores keep a cache of already-decoded micro-ops to avoid repeating the work |
| Code density | each instruction costs a full word | common instructions are short, so code can be smaller |
| Constants | limited by the field size (needs extra instructions for big ones) | a full 32-bit constant can sit inside the instruction |
| Middle road | RISC-V "C" extension adds 16-bit forms; ARM Thumb-2 mixes 16 and 32 bits | |

A look at real x86-64 bytes (Intel syntax, from the assembler):

```text
c3                       ret                       1 byte
48 01 d8                 add  rax, rbx             3 bytes
48 8b 43 08              mov  rax, [rbx+8]         4 bytes
b8 01 00 00 00           mov  eax, 1               5 bytes (the 32-bit constant is inside)
b8 78 56 34 12           mov  eax, 0x12345678      5 bytes (stored little-endian, as in tutorial 02)
```

AArch64 instructions are always 32 bits: `add x0, x1, x2` is the word `0x8b020020`, `ret` is `0xd65f03c0`.

### RISC-V: a hand-encodable ISA

RISC-V has a handful of instruction formats, all 32 bits, with the register fields in the **same bit positions** in every format (so the hardware can start reading the register file before it finishes decoding):

```text
 31      25 24   20 19   15 14  12 11      7 6      0
+----------+-------+-------+------+---------+--------+
|  funct7  |  rs2  |  rs1  |funct3|   rd    | opcode |   R-type   add rd, rs1, rs2
+----------+-------+-------+------+---------+--------+
|     imm[11:0]    |  rs1  |funct3|   rd    | opcode |   I-type   addi rd, rs1, imm   lw rd, imm(rs1)
+----------+-------+-------+------+---------+--------+
| imm[11:5]|  rs2  |  rs1  |funct3| imm[4:0]| opcode |   S-type   sw rs2, imm(rs1)
+----------+-------+-------+------+---------+--------+
|            imm[31:12]            |   rd    | opcode |   U-type   lui rd, imm
+----------------------------------+---------+--------+
```

A **B-type** (branches: `beq`, `bne`, `blt`, ...) is like S-type with the immediate bits scrambled (bits 12, 10:5, 4:1, 11), and a **J-type** (`jal`) has a 20-bit immediate. The scrambling keeps the sign bit and the register fields where the hardware wants them.

Encode `add x10, x11, x12` (R-type: funct7 = 0, rs2 = 12, rs1 = 11, funct3 = 0, rd = 10, opcode = 0x33):

```text
funct7   rs2    rs1    funct3  rd     opcode
0000000  01100  01011  000     01010  0110011     =  0x00C58533
```

In memory it is stored little-endian: `33 85 c5 00`. Encode `addi x5, x0, 100` (I-type: imm = 100, rs1 = 0, funct3 = 0, rd = 5, opcode = 0x13): **0x06400293**. Encode `lw x6, 16(x2)` (I-type, funct3 = 2, opcode 0x03): **0x01012303**. The matching `sw x6, 16(x2)` (S-type, the 12-bit immediate is split, 0 in the top part and 16 in the low part): **0x00612823**.

(In assembly listings you will see ABI names: x10 is `a0`, x11 `a1`, x12 `a2`, x5 `t0`, x6 `t1`, x2 `sp`. Section 9 has the table.)

To **decode**, go the other way: take 0x00A58633. The low 7 bits are 0x33 (R-type ALU), rd = bits 11:7 = 12, funct3 = 0, rs1 = 11, rs2 = 10, funct7 = 0, so it is `add x12, x11, x10`.

### Opcode fields and instruction packing

A 32-bit instruction is a **bit budget** shared by the opcode, the register numbers and a constant. With 32 registers each register field costs 5 bits; with 16 registers (x86-64) it costs 4.

```text
RISC-V R-type:   3 registers x 5 bits = 15
                 opcode 7 + funct3 3 + funct7 7 = 17
                 total 32
```

Whatever grows takes bits from something else: more registers leave less room for the constant, and more addressing modes need more bits to say which one is used. That is exactly why a 3-address RISC instruction carries only a 12-bit immediate, and why `lui` exists.

The opcode itself is **layered**. In RISC-V the 7-bit major opcode picks the format and instruction group; `funct3` and `funct7` pick the exact operation inside it (`add` and `sub` have the same major opcode and funct3; only funct7 differs). Layering lets a small field cover hundreds of operations. A related trick is the **expanding opcode**: give common instructions a short opcode and reserve one pattern that means "read more opcode bits". With a 4-bit opcode field, 15 patterns can name three-operand instructions and the 16th (`1111`) can mean "the next 4 bits are more opcode", which frees room for up to 16 two-operand instructions. Fixed-length ISAs use this to fit many operations into a small field; variable-length ISAs get the same effect with extra opcode bytes. **Decoding** is the control unit (tutorial 08) reading those fields: the opcode says which format and which control signals, and the register fields, which sit in the same place in every format, go straight to the register file while the rest is still being decoded.

x86 layers by bytes: optional prefixes, one or more opcode bytes, a **ModRM** byte (which registers and which addressing mode), an optional **SIB** byte (scale, index, base), then the displacement and the immediate. Two real examples:

```text
48 8b 43 08      mov rax, [rbx+8]
 |  |  |  +-- displacement = 8
 |  |  +----- ModRM 01 000 011: mod=01 (8-bit displacement), reg=000 (rax), rm=011 (rbx)
 |  +-------- opcode 8b: load a register from r/m
 +----------- REX.W prefix: 64-bit operand

8b 04 b7         mov eax, [rdi+rsi*4]
    |  +-- SIB 10 110 111: scale=10 (x4), index=110 (rsi), base=111 (rdi)
    +----- ModRM 00 000 100: rm=100 means "a SIB byte follows"
```

### The constant problem

An I-type immediate is only 12 bits: **-2048 to +2047**. How do you load `0x12345678`? Two instructions: `lui` sets the upper 20 bits, `addi` adds the lower 12.

```text
lui  x5, 0x12345        x5 = 0x12345000
addi x5, x5, 0x678      x5 = 0x12345678
```

One trap, straight from tutorial 02: `addi` **sign-extends** its immediate. If the low 12 bits are 0x800 or more (bit 11 set), they count as negative and the `lui` part must be one higher:

```text
0x12345FFF  ->  lui x5, 0x12346  ;  addi x5, x5, -1       (0x12346000 - 1)
0x0000ABCD  ->  lui x5, 0xB      ;  addi x5, x5, -1075    (0xB000 - 1075)
```

The assembler's `li` pseudo-instruction does this arithmetic for you. AArch64 builds constants 16 bits at a time (`mov w0, 22136` then `movk w0, 0x1234, lsl 16`); x86 simply carries the whole 32-bit constant inside the instruction. Branch offsets are limited the same way: a RISC-V conditional branch has a 13-bit signed offset (about plus or minus 4 KiB); `jal` reaches plus or minus 1 MiB.

---

## 5. Addressing modes

An **addressing mode** is a rule for finding an operand. Most of them boil down to computing an **effective address (EA)**:

```text
EA = base register + (index register x scale) + displacement
```

| Mode | Where the operand is | Typical C | Example |
|---|---|---|---|
| **Immediate** | in the instruction itself | `x + 5` | `add eax, 5` |
| **Register** | in a register | `x + y` | `add eax, ebx` |
| **Direct / absolute** | memory at an address written in the instruction | a global in old non-PIE code | `mov eax, [0x601040]` |
| **Register indirect** | memory at the address held in a register | `*p` | `mov eax, [rdi]` |
| **Displacement** (base + offset) | memory at register + constant | `p->y`, `a[3]` | `mov eax, [rdi+8]` |
| **Indexed** (base + index, scaled) | memory at base + index x scale | `a[i]` | `mov eax, [rdi+4*rsi]` |
| **PC-relative** | memory or target at PC + offset | branches, globals in PIC code | `jne -11`, `mov eax, [rip+g]` |
| **Auto-increment / decrement** | like register indirect, then the register is updated | `*p++` | `ldr w1, [x2], #4` |
| **Memory indirect** | memory holds the address of the operand | `**pp` | `call [rax+8]` (virtual call) |

Textbooks split two of these hairs: **base-register** addressing is a register plus a small offset (the struct-field case), and **indexed** addressing is a fixed array address plus an index register (the array case). Real ISAs merge them into `base + index x scale + displacement`, and x86 offers the whole formula in one operand. RISC-V offers only `register + 12-bit displacement`.

Now the real thing. The same four C functions, three ISAs (clang 18, `-O1`):

```c
struct S { long x; int y; };
int  idx(int *a, long i)  { return a[i]; }        // indexed
int  field(struct S *p)   { return p->y; }        // displacement
int  times5(int x)        { return x * 5; }
int  g;
int  getg(void)           { return g; }           // global variable, position-independent code
```

```text
x86-64 (Intel)                              AArch64                          RISC-V
idx:    mov eax, [rdi + 4*rsi]              ldr w0, [x0, x1, lsl #2]         slli a1, a1, 2
        ret                                 ret                              add  a0, a0, a1
                                                                             lw   a0, 0(a0)
field:  mov eax, [rdi + 8]                  ldr w0, [x0, #8]                 lw   a0, 8(a0)
times5: lea eax, [rdi + 4*rdi]              add w0, w0, w0, lsl #2           slli a1, a0, 2
                                                                             addw a0, a1, a0
getg:   mov eax, [rip + g]                  adrp x8, g                       auipc a0, %pcrel_hi(g)
                                            ldr  w0, [x8, :lo12:g]           lw    a0, %pcrel_lo(...)(a0)
```

(Each function ends in `ret`, omitted after the first.) What the table shows:

- **Indexed.** x86 and AArch64 do `a[i]` in **one** instruction, scaling the index by the element size. RISC-V has no indexed mode: it shifts, adds, then loads with a zero displacement (3 instructions). RISC-V keeps the hardware simple and bets on the compiler and on instruction fusion.
- **Displacement** is the one mode every ISA has: struct fields and fixed array elements. In the Intel syntax `[rdi + 8]`, RISC-V writes `8(a0)`.
- **PC-relative.** `[rip + g]` means "the address of `g` is the current PC plus a constant that the linker fills in". It makes code **position-independent** (it works wherever the program is loaded). RISC-V and AArch64 can't fit a whole PC-relative address in one instruction, so they use two: `adrp`/`auipc` adds the upper bits to the PC, then the load adds the low 12 bits. Branches work the same way: the jump bytes `75 f5` in x86 code say "jump by -11 bytes from the next instruction", not "jump to address 0x13".
- **Auto-increment** (post-index) in AArch64: `ldr w1, [x2], #4` loads from `x2` and then adds 4 to `x2`. It is exactly `*p++`, and the compiler uses it in array loops (see section 8's listing). The **pre-index** form updates first: `str x0, [sp, #-16]!` subtracts 16 from `sp` and then stores, which is a push; `ldr x0, [sp], #16` is the matching pop (post-index). Those are the auto-decrement and auto-increment modes of older ISAs. x86 has no general auto-increment load; `push`, `pop` and the string instructions (`movs`, `stos`) update their pointers implicitly.
- **Memory indirect** is mostly gone from modern ISAs (a load followed by a use is just two instructions). x86 keeps it for **indirect calls and jumps** through a pointer in memory: virtual functions and PLT/GOT entries use `call [rax+8]` and `jmp [rip+x]`.
- **Direct/absolute** addressing barely exists in 64-bit code: addresses are 64 bits long and wouldn't fit. x86-64 code uses PC-relative; RISC ISAs build addresses with two instructions.

### `lea`: the address calculator

`lea eax, [rdi + 4*rdi]` computes `rdi + 4*rdi` (that is, 5 times rdi) and stores it, **without touching memory**. It is the effective-address circuit used as a free adder-shifter, so the compiler uses it for multiplications like `x*5`, `x*9` (`lea eax, [rdi + 8*rdi]`) and for plain three-operand additions. AArch64 gets the same effect by letting the last register operand of `add` be shifted (`add w0, w0, w0, lsl #2`).

---

## 6. Instruction types

| Type | x86-64 | AArch64 | RISC-V |
|---|---|---|---|
| Data transfer | `mov`, `push`, `pop`, `lea` | `ldr`, `str`, `mov`, `ldp`/`stp` | `lw`, `sw`, `mv` (`addi rd, rs, 0`) |
| Arithmetic | `add`, `sub`, `imul`, `idiv` | `add`, `sub`, `mul`, `sdiv` | `add`, `sub`, `mul`, `div` (M extension) |
| Logical | `and`, `or`, `xor`, `not` | `and`, `orr`, `eor`, `mvn` | `and`, `or`, `xor`, (`not` = `xori rd, rs, -1`) |
| Shift / rotate | `shl`, `shr`, `sar`, `rol`, `ror` | `lsl`, `lsr`, `asr`, `ror` | `sll`, `srl`, `sra` (no rotate in the base ISA) |
| Compare | `cmp`, `test` | `cmp`, `tst` | `slt`, `sltu` (result 0 or 1 in a register) |
| Control transfer | `jmp`, `jcc`, `call`, `ret` | `b`, `b.cond`, `bl`, `ret`, `cbz` | `jal`, `jalr`, `beq`, `bne`, `blt`, `bge` |
| System | `syscall`, `int`, `hlt` | `svc`, `wfi` | `ecall`, `ebreak`, `wfi` |
| Atomics and fences | `lock` prefix, `xchg`, `mfence` | `ldxr`/`stxr`, `ldadd`, `dmb` | `lr`/`sc`, `amoadd`, `fence` (A extension) |
| Floating point and SIMD | SSE, AVX | NEON, SVE | F, D, V extensions |

Shift left and right, and the logical operators, are the tutorial 02 operations done in hardware (tutorial 03). Atomics and fences get their own tutorial (20); SIMD another (21).

**Privileged instructions.** Some instructions work only in kernel mode (halting the core with `hlt`, writing the page-table base or interrupt-control registers); user code that tries one gets an exception. The privilege levels are part of the ISA: x86 has rings 0 to 3 (only 0 and 3 are used in practice), AArch64 has exception levels EL0 (user), EL1 (kernel), EL2 (hypervisor) and EL3 (secure monitor), and RISC-V has machine, supervisor and user modes. Tutorial 26 covers how the CPU enters and leaves the kernel.

### Control flow: three design styles

The biggest design difference among the instructions is how a conditional branch learns the condition:

1. **Condition codes (flags).** An arithmetic instruction sets flags (zero, sign, carry, overflow; tutorial 03) and a later conditional branch tests them. x86: `cmp rax, rcx` then `jne loop`. AArch64: `cmp` or `subs` then `b.ne`. Compact, but every flag-setting instruction creates a hidden dependency that the hardware must track (flags get renamed like registers; tutorial 11).
2. **Compare-and-branch.** One instruction compares two registers and branches: RISC-V `bne a0, a1, loop`, MIPS `bne`. No flag register at all; the branch distance is smaller because the instruction has to hold two register fields.
3. **Predication** (condition on every instruction): early ARM, and the Itanium; mostly gone from newer ISAs.

**Procedure calls.** `call` saves the return address, `ret` uses it: x86 pushes it **on the stack**; AArch64 `bl` puts it in the **link register** (x30); RISC-V `jal` puts it in `ra` (x1). Which registers a function must preserve (**callee-saved**) and which it may overwrite (**caller-saved**), plus full stack frames, are calling-convention matters for tutorial 07.

**Branch delay slots.** Some early RISC ISAs (MIPS, SPARC) executed the instruction *after* a branch regardless of the outcome, to hide the pipeline's branch latency. It exposed a pipeline detail in the ISA and became a burden once cores changed; newer ISAs don't have it (tutorials 09 and 10).

---

## 7. Byte order and alignment in the ISA

The ISA also fixes the rules from tutorial 02: x86-64, AArch64 as run by Linux, macOS and Android, and RISC-V are all **little-endian** (AArch64 can be configured big-endian, but almost nothing uses it). Alignment is also an ISA decision: x86 allows most unaligned loads and stores (at a speed cost when they cross a cache line); on others, unaligned access may be slow, may trap, or may need a different instruction, and **atomic** operations nearly always require natural alignment. If you write portable C, don't assume.

---

## 8. RISC versus CISC

**CISC** (complex instruction set computer) grew up when memory was small and slow and compilers were weak: pack a lot of work into one instruction, with many addressing modes, variable length, and memory operands in ALU instructions. x86 and the VAX are the examples. **RISC** (reduced instruction set computer) said: keep instructions simple and regular, so the hardware can be pipelined and clocked fast, and let the compiler do the work. MIPS, SPARC, early ARM and RISC-V follow it.

The tradeoff in the iron law from tutorial 01:

```text
time  =  instructions  x  cycles per instruction (CPI)  x  cycle time
```

CISC tries to lower the instruction count. RISC accepts more instructions to get a lower CPI and a shorter cycle.

| | Classic CISC | Classic RISC |
|---|---|---|
| Instruction length | variable | fixed |
| Memory operands in ALU instructions | yes | no (load/store only) |
| Addressing modes | many | few |
| Registers | few | many (32) |
| Decoder | complex | simple |

### What is true today

The labels have blurred, and the slogans ("RISC is faster", "x86 wastes power on CISC") mislead:

- **x86 is RISC inside.** The front end decodes each x86 instruction into one or more simple **micro-ops** (µops) and the core executes those. `add [mem], eax` is one instruction but the core splits it into a load µop, an add µop and a store; a very complex instruction (`rep movsb`, `cpuid`) may be a sequence from a **microcode** ROM (tutorial 08). The CISC encoding survives mostly for compatibility.
- **"RISC" ISAs grew.** AArch64 has hundreds of instructions, shifted and extended operands, load/store pairs, and bit-field instructions. Cores also **fuse** common pairs (compare + branch) back into one micro-op.
- **The ISA rarely decides speed.** Fast x86 cores and fast ARM cores (Apple's, for example) both exist, and what separates one design from another is mostly the microarchitecture (width, caches, branch prediction), not the instruction set. The ISA's lasting effects are decode complexity (more area and power on small cores), the memory-ordering model (tutorial 20), the software ecosystem, and licensing.

### A measurement (one small function, take with a grain of salt)

The earlier `sum(int *a, int n)` loop, compiled with the **same compiler** (clang 18, `-O1`) for three ISAs:

```text
x86-64 (Intel syntax)         AArch64                     RISC-V (rv64g)
sum:                          sum:                        sum:
  test esi, esi                 cmp  w1, #1                 li   a2, 0
  jle  .Lzero                   b.lt .Lzero                 blez a1, .Ldone
  mov  ecx, esi                 mov  x8, x0                 slli a1, a1, 2
  xor  edx, edx                 mov  w0, wzr                add  a1, a0, a1
  xor  eax, eax                 mov  w9, w1               .Lloop:
.Lloop:                       .Lloop:                       lw   a3, 0(a0)
  add  eax, [rdi + 4*rdx]       ldr  w10, [x8], #4          addi a0, a0, 4
  inc  rdx                      subs x9, x9, #1             addw a2, a3, a2
  cmp  rcx, rdx                 add  w0, w10, w0            bne  a0, a1, .Lloop
  jne  .Lloop                   b.ne .Lloop               .Ldone:
  ret                           ret                         mv   a0, a2
.Lzero:                       .Lzero:                       ret
  xor  eax, eax                 mov  w0, wzr
  ret                           ret
```

| | x86-64 | AArch64 | RISC-V rv64g | RISC-V rv64gc (compressed) |
|---|---|---|---|---|
| Instructions | 12 | 12 | 10 | 10 |
| Code bytes | **25** | 48 | 40 | **24** |
| Average bytes per instruction | 2.1 | 4.0 | 4.0 | 2.4 |
| Loop body instructions | 4 | 4 | 4 | 4 |

(The x86 object code also contains a 6-byte `nop` that the assembler inserted to align the loop start; it is padding, not part of the function's logic, so it is left out above. Counting it gives 13 instructions and 31 bytes.)

Notice: the variable-length x86 is much *denser* than the fixed-length AArch64 and RISC-V base (25 bytes against 48 and 40), and RISC-V with 16-bit compressed forms ties with it (24). And the "reduced" ISAs did **not** need more instructions here: RISC-V needed the fewest, because compare-and-branch fuses the test into the branch and the loop uses no flags. Each ISA picks different trade-offs; none wins on every metric. (One loop, one compiler; don't generalise.)

**VLIW** (very long instruction word) is the other path: the compiler packs several independent operations into one wide instruction. It lives on in DSPs but failed on general-purpose CPUs (Intel's Itanium); it comes up again with out-of-order execution in tutorial 11.

---

## 9. RISC-V as the teaching ISA

We use RISC-V for the next tutorials (the classic five-stage RISC pipeline in tutorial 09 is easiest to draw with RISC-V-style instructions) because it is small, regular, open and has no historical baggage. The base integer ISA (**RV32I**, or **RV64I** for 64-bit) is under 50 instructions; everything else is an optional **extension**, named by a letter: **M** multiply/divide, **A** atomics, **F**/**D** single/double floating point, **C** 16-bit compressed instructions, **V** vectors. "G" is shorthand for the general-purpose bundle (I, M, A, F, D), so `rv64g` is what we compiled above.

**Registers.** 32 integer registers `x0` to `x31`, written in code with ABI names:

| Register | ABI name | Role |
|---|---|---|
| x0 | `zero` | always 0 |
| x1 | `ra` | return address |
| x2 | `sp` | stack pointer |
| x3, x4 | `gp`, `tp` | global pointer, thread pointer |
| x5 to x7, x28 to x31 | `t0` to `t6` | temporaries (caller-saved) |
| x8, x9, x18 to x27 | `s0` to `s11` | saved registers (callee-saved) |
| x10 to x17 | `a0` to `a7` | function arguments and return values |

**Instructions to recognise** (enough to read most RISC-V code):

| Instruction | Meaning |
|---|---|
| `add`, `sub`, `and`, `or`, `xor`, `sll`, `srl`, `sra` | register-register ALU: `rd = rs1 op rs2` |
| `addi`, `andi`, `ori`, `xori`, `slli`, `srli`, `srai` | register-immediate ALU (12-bit immediate) |
| `lw`, `lh`, `lb`, `ld` / `sw`, `sh`, `sb`, `sd` | load / store 32, 16, 8, 64 bits: `lw rd, imm(rs1)` |
| `beq`, `bne`, `blt`, `bge` | compare two registers and branch (PC-relative) |
| `jal`, `jalr` | jump and link (call), jump to a register (return, indirect call) |
| `lui`, `auipc` | upper-immediate; `auipc` adds it to the PC |
| `ecall` | system call / trap into the operating system |

**Pseudo-instructions** are assembler conveniences that expand to real instructions: `li` (see section 4), `mv rd, rs` = `addi rd, rs, 0`, `not rd, rs` = `xori rd, rs, -1`, `nop` = `addi x0, x0, 0`, `j label` = `jal x0, label`, `ret` = `jalr x0, 0(ra)` (the word `0x00008067`).

**Reading the compiled `sum`** above, line by line: `li a2, 0` sets the accumulator to 0 (a0 = array pointer, a1 = n); `blez a1, .Ldone` skips the loop if n is 0 or less; `slli a1, a1, 2` then `add a1, a0, a1` compute the end pointer (array + 4n); in the loop `lw a3, 0(a0)` loads an element, `addi a0, a0, 4` advances the pointer, `addw a2, a3, a2` adds it to the sum, and `bne a0, a1, .Lloop` repeats until the pointer reaches the end; `mv a0, a2` puts the sum in the return register and `ret` returns. That is the whole function: four instructions per element.

---

## 10. Mistakes that cost points

1. **"RISC means fewer instructions" or "RISC code is bigger".** RISC means simple, regular instructions. In the measurement above the RISC-V loop had the fewest instructions, and code size depended on the encoding, not the label.
2. **"x86 is slow because it is CISC."** Modern x86 cores translate to micro-ops and run as fast as anything. Compare microarchitectures, not slogans.
3. **Thinking `lea` reads memory.** It only computes the address.
4. **Forgetting sign extension in `lui` + `addi`.** For constants with bit 11 set, the `lui` value is one higher.
5. **Mixing assembly syntaxes.** Intel syntax is `mov dest, src`; AT&T (the default for `gcc -S` on Linux) is `mov src, dest` with `%` register prefixes. Use `-masm=intel` for the first one.
6. **Assuming unaligned access works everywhere.** It does on x86; on others it can be slow, trap, or not be atomic.
7. **Confusing ISA with microarchitecture.** Pipeline depth and cache size are not in the ISA.
8. **Mixing up address count and ISA class.** x86 is register-memory and 2-address.

---

## 11. Questions you will be asked

**What is an ISA, and how does it differ from the microarchitecture?** The ISA is the programmer-visible contract: instructions, registers, addressing, memory model, exceptions. The microarchitecture is one way of implementing it: pipeline, caches, issue width. Many chips implement the same ISA.

**RISC versus CISC: what is the real difference today?** RISC is load/store with simple fixed-length instructions; CISC has memory operands, variable length and many addressing modes. But x86 cores decode into RISC-like micro-ops, so speed differences come from microarchitecture. The ISA still affects decoder cost, code size, the memory-ordering model and the ecosystem.

**Why do x86 processors translate into micro-ops?** To get a regular, simple, fixed-format internal instruction that can be pipelined, renamed and executed out of order, while staying compatible with a 40-year-old variable-length ISA. A micro-op cache saves repeating the expensive decode.

**Why did load/store architectures win?** Uniform simple instructions are easy to pipeline and decode; registers keep values close; memory faults are confined to loads and stores.

**Fixed versus variable-length instructions?** Fixed: trivial to find boundaries and decode in parallel, but larger code and limited immediates. Variable: denser code and big inline constants, but a complex decoder. Compressed extensions (RISC-V C, Thumb-2) mix the two.

**Name the addressing modes.** Immediate, register, direct, register indirect, memory indirect, displacement (base + offset), indexed (base + scaled index), PC-relative, auto-increment/decrement. x86 combines them into `[base + index*scale + disp]`; RISC-V has only register + 12-bit displacement.

**What does `lea` do?** Computes an effective address and stores it in a register without a memory access. Compilers use it for fast shift-and-add arithmetic.

**Flags versus compare-and-branch?** Flags make branches compact but add a shared implicit state that hardware has to track; compare-and-branch (RISC-V, MIPS) fuses the test into the branch and uses no flag register, at the cost of shorter branch range and more fields per instruction.

**How do you load a 32-bit constant on RISC-V?** `lui` for the upper 20 bits and `addi` for the lower 12, adjusting the `lui` value by one when bit 11 of the constant is set (the `addi` immediate is sign-extended).

---

## 12. Problems

**How to use this section.** Try each problem before looking at the answers at the bottom. A, B and C are paper problems (about 25 minutes each); D uses a compiler (about 30 minutes).

### Problem A (paper): address formats and the stack machine

(a) Write `R = (P - Q) * (S + T) + U` for a 3-address, a 2-address (destination is also the first source), a 1-address (accumulator) and a 0-address (stack) machine. Count the instructions in each.
(b) For the stack program, give the contents of the stack after each instruction with P = 7, Q = 3, S = 2, T = 4, U = 1.
(c) Why do the arithmetic instructions of a stack machine not need operand fields, and why is this style hard to execute in parallel?

*Where:* paper. *How:* the sequences in section 3 are the template. For the stack, work like a reverse-Polish calculator: operands are pushed, an operator pops two and pushes the result. For (c), think about what every instruction needs as input.

### Problem B (paper): encode and decode RISC-V

Use the formats in section 4.
(a) Encode `sub x7, x8, x9` (R-type; `sub` differs from `add` only in funct7, which is 0100000).
(b) Encode `addi x10, x10, -1`.
(c) Decode the word `0x0085A283`.
(d) Load `0x0000ABCD` into x5 with `lui` + `addi`. Which immediates do you use, and why isn't the `lui` value simply 0xA?
(e) How far can a 13-bit signed branch offset reach?

*Where:* paper; check your answers with `llvm-mc --triple=riscv32 --show-encoding` if you have LLVM installed, or in the online assembler at godbolt.org (binary view). *How:* fill in the fields bit by bit, then regroup into hex. For (b) the immediate is -1, all ones in 12 bits. For (d) the low 12 bits of 0xABCD are 0xBCD, with bit 11 set.

### Problem C (paper): pick the addressing mode

For each C fragment, give the addressing mode and the x86-64 operand (Intel syntax). Assume `rdi` holds the pointer and `rsi` holds `i`, with 4-byte `int` elements unless noted.
(a) `x = a[i];`
(b) `x = p->count;` where `count` is a `long` at offset 24 in the struct
(c) `x = *p;`
(d) `x = g;` in position-independent code, `g` a global `int`
(e) `y = x * 9;` using one instruction without a multiplier
(f) How many RISC-V instructions does (a) take, and what is the reason?

*Where:* paper. *How:* write the effective-address formula first (base + index x scale + displacement), then decide which terms are present.

### Problem D (hands-on): compare three ISAs

Compile this function for x86-64, AArch64 and RISC-V and compare the output:

```c
int sum(int *a, int n) {
    int s = 0;
    for (int i = 0; i < n; i++) s += a[i];
    return s;
}
```

(1) How many instructions and how many bytes per target? (2) Which addressing mode does each use for the array element? (3) How does each ISA do the loop-exit test: flags or compare-and-branch? (4) Which ISAs have fixed-length instructions, and how can you tell from the bytes?

*Where:* godbolt.org (Compiler Explorer): choose C, add three compiler panes (x86-64 clang, ARM64 clang, RISC-V rv64 clang), options `-O1`, and tick "Compile to binary object" to see the bytes. Or locally on your Mac: `clang -O1 -S -o - -arch arm64 sum.c` and `-arch x86_64` (add `-mllvm -x86-asm-syntax=intel` for Intel syntax). Apple's clang may lack the RISC-V target; use godbolt for that one. *How:* count the lines of the function body, sum the byte columns, then look for `[...]` operands.

---

## Answers

Try before reading.

### Problem A

(a)

```text
3-address:   SUB T1, P, Q        2-address:   MOV T1, P          1-address:   LOAD P
             ADD T2, S, T                     SUB T1, Q                       SUB Q
             MUL T1, T1, T2                   MOV T2, S                       STORE T1
             ADD R, T1, U                     ADD T2, T                       LOAD S
                                              MUL T1, T2                      ADD T
0-address:   PUSH P                           ADD T1, U                       MUL T1
             PUSH Q                           MOV R, T1                       ADD U
             SUB                                                              STORE R
             PUSH S
             PUSH T
             ADD
             MUL
             PUSH U
             ADD
             POP R
```

Counts: 3-address **4**, 2-address **7**, 1-address **8**, 0-address **10**.

(b) Stack after each instruction (top on the right): push P `[7]`; push Q `[7,3]`; SUB `[4]`; push S `[4,2]`; push T `[4,2,4]`; ADD `[4,6]`; MUL `[24]`; push U `[24,1]`; ADD `[25]`; POP R leaves `[]` and R = 25. Check: (7-3)*(2+4)+1 = 25.

(c) The operands are implicit: always the top two stack entries, so the opcode alone is enough. But that same property means each instruction depends on the one before it through the stack top, so there is little independent work to overlap; real stack machines (and JVMs) map the stack onto registers.

### Problem B

(a) `sub x7, x8, x9`: funct7 = 0100000, rs2 = 9 (01001), rs1 = 8 (01000), funct3 = 000, rd = 7 (00111), opcode = 0110011, which is **0x409403B3**.

(b) `addi x10, x10, -1`: imm = 111111111111, rs1 = 10, funct3 = 000, rd = 10, opcode = 0010011, which is **0xFFF50513**.

(c) `0x0085A283`: opcode 0x03 (load), rd = 5, funct3 = 2 (word), rs1 = 11, imm = 8, so **`lw x5, 8(x11)`** (that is `lw t0, 8(a1)`).

(d) The low 12 bits 0xBCD have bit 11 set, so `addi` sees them as 0xBCD - 0x1000 = -1075 (negative). The `lui` value must compensate: (0xABCD + 1075) >> 12 = 0xB. So `lui x5, 0xB` (x5 = 0xB000) and `addi x5, x5, -1075` give 0xB000 - 1075 = 0xABCD. A `lui x5, 0xA` would give 0xA000 - 1075 = 0x9BCD, which is wrong.

(e) The 13-bit signed offset (always even, bit 0 is implicit) reaches **-4096 to +4094 bytes**, about plus or minus 4 KiB. Farther jumps need `jal` (plus or minus 1 MiB) or an `auipc` + `jalr` pair.

### Problem C

(a) Indexed: **`[rdi + 4*rsi]`** (base + index x scale), e.g. `mov eax, [rdi + 4*rsi]`.
(b) Displacement: **`[rdi + 24]`**.
(c) Register indirect: **`[rdi]`**.
(d) PC-relative: **`[rip + g]`**; the linker fills in the distance from the PC to `g`.
(e) **`lea eax, [rdi + 8*rdi]`**: rdi + 8 x rdi = 9 x rdi, no memory access.
(f) **Three** on RISC-V (`slli`, `add`, `lw`): the only memory addressing mode is register + 12-bit displacement, so the scaled index has to be computed separately.

### Problem D

My results (clang 18, `-O1`, the same function on each target):

| | x86-64 | AArch64 | RISC-V rv64g |
|---|---|---|---|
| Instructions | 12 | 12 | 10 |
| Code bytes | 25 (31 with a 6-byte alignment `nop` the assembler adds) | 48 | 40 |
| Array element | `add eax, [rdi + 4*rdx]` (indexed, folded into the add) | `ldr w10, [x8], #4` (post-increment) | `lw a3, 0(a0)` (displacement; the pointer is bumped by `addi`) |
| Loop test | `cmp` + `jne` (flags) | `subs` + `b.ne` (flags; the counter and the test in one instruction) | `bne a0, a1, ...` (compare-and-branch) |
| Instruction length | variable (1 to 3 bytes here, apart from the 6-byte alignment `nop`) | fixed 4 bytes | fixed 4 bytes (2 or 4 with the C extension) |

How to see fixed length from the bytes: in the AArch64 and RISC-V disassembly every address is a multiple of 4 and every instruction shows exactly 8 hex digits; in the x86 listing consecutive addresses differ by 1 to 3 (the alignment `nop` is the odd one out at 6). Expect small differences if your compiler version differs, but the same pattern: x86 packs more per byte, RISC-V needs the fewest instructions, and only x86 reads memory inside an ALU instruction.

---

## Cheat sheet

- **ISA** = programmer-visible contract (instructions, registers, addressing, memory model, exceptions). **Microarchitecture** = how it's built.
- **Classes:** stack (0-address), accumulator (1), register-memory (x86, 2-address), memory-memory (dead), **load/store** (ARM, RISC-V: 3-address, only loads and stores touch memory).
- More operand fields = fewer instructions, longer encodings. `(A+B)*(C-D)`: 3, 6, 7, 8 instructions for 3-, 2-, 1-, 0-address.
- **Fixed length** (AArch64, RISC-V): easy decode, bigger code. **Variable** (x86, 1 to 15 bytes): denser, harder decode. Compressed forms mix.
- **RISC-V R-type:** `funct7 | rs2 | rs1 | funct3 | rd | opcode`; I-type has a 12-bit immediate (-2048..2047); constants: `lui` + `addi`, and **add 1 to the lui value if bit 11 of the low part is set**. Registers x10 to x17 are `a0` to `a7`; x0 is zero.
- **Addressing modes:** immediate, register, direct, register indirect, memory indirect, displacement, indexed (base + index x scale), PC-relative, auto-increment. EA = base + index x scale + displacement. x86 and AArch64 fold the scaling; RISC-V only has register + displacement.
- **`lea`** computes an address and does not touch memory (fast `x*5`, `x*9`).
- **Branches:** flags (x86, ARM) or compare-and-branch (RISC-V). Calls: x86 pushes the return address, ARM and RISC-V use a link register (`x30`, `ra`).
- **RISC vs CISC:** time = instructions x CPI x cycle time. Today x86 is RISC-like inside (micro-ops); speed comes from microarchitecture, not slogans. In the measured loop: x86 25 bytes, AArch64 48, RISC-V 40 (24 with compressed).

**Next:** tutorial 07, Assembly in Practice: x86-64 and AArch64 (registers and flags, calling conventions, stack frames, reading compiler output at -O0, -O1 and -O2, inline assembly, and the assembler, linker and loader). Say "next" when you are ready.
