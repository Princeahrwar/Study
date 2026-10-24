# CO 01: How a Computer Works

*Part of the [Computer Organization plan](00_plan.md) · Time: about 3 hours (1.5 reading, 1.5 problems) · You need: nothing, a little C helps*

By the end of this tutorial you can:

- name the parts of a computer and say what each one does;
- explain what the CPU does with a program, one instruction at a time (fetch, decode, execute);
- read a few lines of real x86-64 and ARM64 assembly and see the same ideas you traced on a toy machine;
- use the iron law (time = instructions × CPI × cycle time) to compare two machines;
- explain why memory, not the CPU, decides most performance questions.

---

## 1. The big picture

A computer has a few parts, and everything else in this track is a closer look at one of them.

| Part | What it does | Example |
|---|---|---|
| **CPU** | Runs instructions, one after another | x86-64 core, ARM64 core |
| **Memory (RAM)** | Holds the running program and its data. Fast, but it forgets everything when the power goes off | DDR5 DRAM |
| **Storage** | Keeps data permanently. Slower than RAM | SSD, flash chip |
| **I/O devices** | Connect to the outside world | network card, USB, display, Wi-Fi radio |
| **Interconnect** | The wires (buses) that carry addresses and data between the parts | memory bus, PCIe |

```text
   +---------------------------+
   |            CPU            |
   |  registers   ALU   control|
   +-------------+-------------+
                 |
   ==============+===========================  interconnect (buses)
        |                |               |
   +----+-----+    +-----+-----+    +----+------+
   |  memory  |    |  storage  |    | I/O       |
   |  (RAM)   |    | (SSD)     |    | devices   |
   +----------+    +-----------+    +-----------+
```

The one idea to hold on to: **the program lives in memory, and the CPU fetches its instructions from memory and carries them out.** The CPU itself does nothing else.

---

## 2. Memory is an array of bytes

Memory is a long row of numbered boxes. Each box holds one byte (8 bits). The box number is its **address**.

```text
address    content
0x1000     0x05
0x1001     0x00
0x1002     0x00
0x1003     0x00      <- the int 5 takes these 4 bytes (the low byte comes first: little-endian)
0x1004     ...
```

Seen from C:

```c
int x = 5;      // x occupies 4 bytes, say at address 0x1000
int *p = &x;    // p is a variable that holds the number 0x1000
int y = *p;     // read 4 bytes starting at the address stored in p
```

A **pointer is just an address stored in a variable**, and `*p` means "go to that address and read". Nothing more mysterious than that.

**Instructions are also numbers stored in memory**, next to the data. This is the *stored-program* idea (the von Neumann architecture): one memory holds both code and data, so loading a different program is just writing different numbers into memory. Some small chips (many microcontrollers) use a *Harvard* design instead, with separate memories for code and data. Modern desktop and server CPUs are von Neumann at the top (one address space) but Harvard-like inside: the first-level cache is split into an instruction cache and a data cache (tutorial 13).

---

## 3. Inside the CPU

A CPU is built from a handful of pieces:

| Piece | Job |
|---|---|
| **Registers** | A few very small, very fast storage slots inside the CPU. A real CPU has about 16 to 32 general-purpose ones |
| **ALU** (arithmetic logic unit) | Does the arithmetic and logic: add, subtract, and, or, shift, compare |
| **Program counter (PC)** | A register holding the **address of the next instruction** |
| **Control unit** | Reads an instruction and tells the other pieces what to do |
| **Flags** | A few bits remembering facts about the last result (was it zero? negative?) so a later branch can use them |

Registers matter because they are *inside* the CPU. Reading a register costs almost nothing; reading memory costs hundreds of cycles (section 10). So compilers keep frequently used variables in registers whenever they can.

### The fetch-decode-execute loop

Every CPU, from a tiny microcontroller to the biggest server chip, does the same thing forever:

```text
1. FETCH     read the instruction at the address in the PC
2. DECODE    work out what it means: which operation, which registers or addresses
3. EXECUTE   do it (the ALU computes, or memory is read or written)
4. UPDATE    PC = address of the next instruction (usually the next one in memory,
             or somewhere else if the instruction was a jump)
5. go back to 1
```

That is all a CPU is: a machine that repeats this billions of times per second. Everything fancier (pipelines, caches, out-of-order execution) is a trick to do the same loop faster without changing what it means.

---

## 4. A toy machine

To see the loop in action, here is a made-up CPU with four registers (`R0` to `R3`) and seven instructions. It is much simpler than a real one, but real instruction sets are built from the same kinds of operations.

| Instruction | Meaning |
|---|---|
| `LOAD Rd, [addr]` | `Rd = memory[addr]` |
| `STORE [addr], Rs` | `memory[addr] = Rs` |
| `LOADI Rd, n` | `Rd = n` (a constant) |
| `ADD Rd, Ra, Rb` | `Rd = Ra + Rb` |
| `SUB Rd, Ra, Rb` | `Rd = Ra - Rb` |
| `JNZ Rs, target` | if `Rs` is not zero, set `PC = target`; otherwise go on to the next instruction |
| `HALT` | stop |

### Program 1: add two numbers from memory

Memory starts with `memory[100] = 5` and `memory[101] = 7`. Each instruction sits at its own address (0, 1, 2, ...), and the PC starts at 0.

```text
addr  instruction
0     LOAD  R1, [100]
1     LOAD  R2, [101]
2     ADD   R3, R1, R2
3     STORE [102], R3
4     HALT
```

| PC | Instruction | Effect | Registers afterwards |
|---|---|---|---|
| 0 | `LOAD R1, [100]` | R1 = 5 | R1=5 |
| 1 | `LOAD R2, [101]` | R2 = 7 | R1=5, R2=7 |
| 2 | `ADD R3, R1, R2` | R3 = 12 | R1=5, R2=7, R3=12 |
| 3 | `STORE [102], R3` | memory[102] = 12 | (unchanged) |
| 4 | `HALT` | stop | |

Five instructions, and `memory[102]` now holds 12. The PC went 0, 1, 2, 3, 4: straight down the list.

### Program 2: a loop

The sum 1 + 2 + ... + N, here with N = 3. The only way to *change the order* is a jump, and `JNZ` is the one jump here.

```text
addr  instruction            what it does
0     LOADI R1, 3            R1 = counter (starts at N)
1     LOADI R2, 0            R2 = running sum
2     LOADI R3, 1            R3 = the constant 1
3     ADD   R2, R2, R1       sum = sum + counter        <- loop body starts here
4     SUB   R1, R1, R3       counter = counter - 1
5     JNZ   R1, 3            if counter != 0, go back to address 3
6     STORE [200], R2        store the result
7     HALT
```

| Step | PC | Instruction | R1 (counter) | R2 (sum) |
|---|---|---|---|---|
| 1 to 3 | 0, 1, 2 | the three `LOADI`s | 3 | 0 |
| 4 | 3 | `ADD` | 3 | 3 |
| 5 | 4 | `SUB` | 2 | 3 |
| 6 | 5 | `JNZ`: R1 is 2, so jump to 3 | 2 | 3 |
| 7 | 3 | `ADD` | 2 | 5 |
| 8 | 4 | `SUB` | 1 | 5 |
| 9 | 5 | `JNZ`: R1 is 1, jump to 3 | 1 | 5 |
| 10 | 3 | `ADD` | 1 | 6 |
| 11 | 4 | `SUB` | 0 | 6 |
| 12 | 5 | `JNZ`: R1 is 0, **do not jump**, PC = 6 | 0 | 6 |
| 13 | 6 | `STORE [200], R2` | 0 | 6 |
| 14 | 7 | `HALT` | | |

The result is 6, and the machine executed **14 instructions**. In general it is 3 + 3N + 2 = **3N + 5**, so N = 10 takes 35. That count is the "instructions" part of the iron law in section 9.

Three things to take away:

- A `for` or `while` loop is a conditional jump **backwards**; an `if` is a conditional jump **forwards**. There is no other control flow at this level.
- The PC is just a register. Changing it is what a jump does.
- A loop that tests at the bottom always runs once, even if the counter starts at zero (Problem A asks you what that does).

---

## 5. The same ideas in real assembly

Real CPUs have more instructions and many more registers, but the pattern is the same. Here is a C function and what a compiler (GCC with `-O1`) makes of it for the two architectures you will meet most: **x86-64** (most servers and PCs) and **ARM64** (phones, Apple silicon, most Wi-Fi and embedded SoCs). Both write the destination first.

```c
void add_mem(int *a, int *b, int *c) { *c = *a + *b; }
```

**x86-64** (Intel syntax):

```text
add_mem:
    mov  eax, DWORD PTR [rsi]       ; eax = *b
    add  eax, DWORD PTR [rdi]       ; eax = eax + *a   (reads memory directly)
    mov  DWORD PTR [rdx], eax       ; *c = eax
    ret
```

**ARM64:**

```text
add_mem:
    ldr  w0, [x0]                   ; w0 = *a
    ldr  w1, [x1]                   ; w1 = *b
    add  w0, w0, w1                 ; w0 = w0 + w1
    str  w0, [x2]                   ; *c = w0
    ret
```

Compare with Program 1: it is the same `LOAD`, `LOAD`, `ADD`, `STORE`. The pointers `a`, `b` and `c` arrive in registers (`rdi, rsi, rdx` on x86-64, `x0, x1, x2` on ARM64; tutorial 07 explains the rules). Two differences worth noticing:

- **ARM64 is a load/store architecture**: only `ldr` and `str` touch memory, and `add` works on registers. **x86-64 lets `add` read memory directly** (`add eax, [rdi]` is a load and an add in one instruction).
- `w0` is the lower 32 bits of the 64-bit register `x0`, and `eax` is the lower 32 bits of `rax`. An `int` is 32 bits, so the compiler uses those halves.

Now a loop, in the same spirit as Program 2:

```c
int sum(const int *a, int n) {
    int s = 0;
    for (int i = 0; i < n; i++) s += a[i];
    return s;
}
```

**x86-64:**

```text
sum:
    test esi, esi                   ; n <= 0 ?
    jle  .L5                        ; yes: return 0
    mov  rax, rdi                   ; p = a
    movsx rsi, esi                  ; widen n to 64 bits
    lea  rcx, [rdi+rsi*4]           ; end = a + n
    mov  edx, 0                     ; s = 0
.L4:
    add  edx, DWORD PTR [rax]       ; s += *p
    add  rax, 4                     ; p++  (4 bytes)
    cmp  rax, rcx                   ; p == end ?
    jne  .L4                        ; no: loop again
.L2:
    mov  eax, edx                   ; return s
    ret
.L5:
    mov  edx, 0
    jmp  .L2
```

**ARM64:**

```text
sum:
    cmp  w1, 0                      ; n <= 0 ?
    ble  .L5                        ; yes: return 0
    mov  x2, x0                     ; p = a
    add  x3, x0, w1, sxtw 2         ; end = a + n*4
    mov  w0, 0                      ; s = 0
.L4:
    ldr  w1, [x2], 4                ; w1 = *p, then p += 4
    add  w0, w0, w1                 ; s += w1
    cmp  x2, x3                     ; p == end ?
    bne  .L4                        ; no: loop again
.L2:
    ret
.L5:
    mov  w0, 0
    b    .L2
```

Read the loop (`.L4`) of the x86 version as the toy loop:

- The loop body is **four instructions**: add the element, move the pointer, compare, branch. `jne .L4` is a `JNZ` that jumps backwards.
- The compiler did not keep an index `i`. It turned `a[i]` into a **pointer `p` that moves 4 bytes at a time** (an `int` is 4 bytes) and compares it with the address just past the end. This is the pointer-walk form of an array loop.
- `test esi, esi / jle .L5` at the top is the check that avoids the "runs once even when empty" problem from section 4.
- `lea rcx, [rdi+rsi*4]` computes the address `a + n*4` and stores it in `rcx` without touching memory. Compilers also use `lea` as a cheap general-purpose adder.
- `cmp` sets the flags, and `jne` / `bne` reads them. That is the "flags" box from section 3.

You do not need to memorise any of this yet. The point is that it is the same fetch-decode-execute loop on the same kind of instructions, just with more registers and a larger menu.

---

## 6. Kinds of instructions

Every instruction set, however big, comes down to four groups:

| Group | x86-64 examples | ARM64 examples | What it does |
|---|---|---|---|
| **Arithmetic and logic** | `add`, `sub`, `and`, `shl`, `cmp` | `add`, `sub`, `and`, `lsl`, `cmp` | Work on registers; may set flags |
| **Data movement** | `mov`, `push`, `pop` | `ldr`, `str`, `mov` | Copy between registers and memory |
| **Control flow** | `jmp`, `jne`, `call`, `ret` | `b`, `bne`, `bl`, `ret` | Change the PC: loops, `if`, function calls |
| **System** | `syscall`, `hlt`, `int` | `svc`, `wfi` | Talk to the operating system or the hardware |

A **function call** is a jump that also remembers where to come back to (`call` and `bl` save the return address; `ret` jumps to it). More on that in tutorial 07.

Instructions are numbers too. These two are the same operation, `eax = eax + ebx` and `w0 = w0 + w1`, as the actual bytes:

```text
x86-64   add eax, ebx       ->  01 d8             (2 bytes; x86 instructions are 1 to 15 bytes long)
ARM64    add w0, w0, w1     ->  0x0b010000        (a 32-bit word; every ARM64 instruction is exactly 4 bytes)
```

---

## 7. From C source to a running program

The CPU never sees C. A program goes through a pipeline of tools first:

```text
hello.c --compiler--> assembly --assembler--> object file --linker--> executable
                                                                          |
          the OS loads it into memory, sets the PC to the entry point  <--+
                                  |
                       the CPU runs fetch-decode-execute from there
```

- The **compiler** translates C into assembly for the target ISA.
- The **assembler** turns assembly text into machine code (the numbers above).
- The **linker** joins your object files with libraries and fixes up addresses.
- The **operating system's loader** copies the program into memory, gives it a stack, and starts it by setting the PC.

This is why the same C file needs recompiling for ARM64 and x86-64: they understand different machine code.

---

## 8. The CPU is not alone: privilege and interrupts

Three ideas you will need all through this track and the OS and driver tracks. Here is just the outline.

**Privilege modes.** A CPU runs in at least two modes. Your programs run in **user mode**, where some instructions and some memory are off limits. The operating system kernel runs in **kernel mode** and can do everything. A program asks the kernel for help (open a file, send a packet) with a special instruction, `syscall` on x86-64 and `svc` on ARM64. That instruction switches to kernel mode and jumps to a fixed address chosen by the OS.

**Interrupts.** Devices need to get the CPU's attention: a packet arrived, a timer expired. A device raises an interrupt, and the CPU, between two instructions, saves the PC, switches to kernel mode and jumps to a **handler** the OS set up. When the handler finishes, execution resumes where it left off. This is also how the OS takes the CPU back from a program that never gives it up: a **timer interrupt** fires every few milliseconds, and the scheduler can decide to run something else.

**Context switch.** To run a different program, the kernel saves the running program's registers and PC in a kernel data structure, loads the other program's saved registers (and, for a different process, switches to its address space), and returns to user mode at the saved PC. The direct cost is on the order of a microsecond, and more if the caches and the TLB (tutorial 15) have to refill afterwards. That is why designs that avoid needless system calls and needless switches run faster. The OS track covers scheduling.

System calls and interrupts are both "the PC changed in a way the program did not ask for", under the hardware's and the OS's control. Tutorial 26 covers them in detail.

---

## 9. How fast? The iron law

How long does a program take? Break it into three factors:

```text
time  =  instructions  ×  cycles per instruction (CPI)  ×  time per cycle
         (how many are      (how many clock ticks each       (1 / clock rate)
          executed)          one needs on average)
```

The **clock** is a signal that ticks at a fixed rate; each tick is one **cycle**. At 3 GHz there are 3 billion cycles per second, so one cycle takes about 0.33 ns. **IPC** (instructions per cycle) is just 1 / CPI.

Example: a program runs 2 billion instructions on a 3 GHz CPU with CPI 1.5.

```text
time = 2×10^9 × 1.5 / (3×10^9) = 1.0 second
```

Now change one factor at a time:

| Change | New time | Why |
|---|---|---|
| Compiler removes 10% of the instructions | 0.9 s | fewer instructions |
| Cache misses push CPI from 1.5 up to 3.0 | 2.0 s | each instruction waits longer |
| Clock goes up 10% (3.3 GHz) | 0.91 s | each cycle is shorter |

Each factor is controlled by a different thing:

| Factor | What controls it |
|---|---|
| Instruction count | the algorithm, the compiler, and the ISA |
| CPI | the microarchitecture: pipelines, caches, branch prediction |
| Cycle time | circuit design, manufacturing technology, heat and power limits |

Two lessons. First, **clock rate alone does not tell you speed**: a 4 GHz chip with CPI 3 loses to a 3 GHz chip with CPI 1. Second, since the clock has stopped climbing (power and heat limit it to about 3 to 5 GHz), most of the remaining progress is in the CPI term, and **most of the work in lowering CPI is about hiding memory delay**. That is the next section, and most of the rest of the track.

Ordinary code often gets an IPC of about 1 to 2 on a modern core, even though the core can in principle do more per cycle when everything goes right.

---

## 10. Memory is the bottleneck: the latency ladder

Not all storage is equally far from the CPU. Typical access times, in round numbers (exact values vary by chip):

| Where the data is | Time | Cycles at 3 GHz | If one cycle were one second |
|---|---|---|---|
| Register | about 0.3 ns | about 1 | a second |
| L1 cache | about 1 ns | about 4 | 4 seconds |
| L2 cache | about 4 ns | about 12 to 15 | 15 seconds |
| L3 cache | about 10 to 15 ns | about 40 | 40 seconds |
| **DRAM (main memory)** | **about 70 to 100 ns** | **about 200 to 300** | **3 to 5 minutes** |
| SSD read | about 20 to 100 µs | 60,000 to 300,000 | about 1 to 3 days |
| Network round trip inside a data centre | about 100 to 500 µs | 300,000 to 1.5 million | about 4 to 17 days |

The last column is the useful one. If a register read is a second, then going to DRAM is a wait of several minutes. A CPU that stalled for every memory access would spend almost all its time waiting.

So CPUs hide the delay with **caches**: small, fast memories that keep copies of recently used data close to the core. They work because programs have **locality**: the data you used a moment ago (or the data next to it) is likely to be used again. The transfer unit is a **cache line**, usually 64 bytes, so reading one `int` also brings its 15 neighbours into the cache. That is why the array walk in DSA tutorial 01 was about 16 times faster by row than by column (10 ms against 165 ms on a cloud VM). Tutorial 13 builds all of this from scratch.

The consequence for how you think about programs: **the number of instructions is often not the cost. The number of cache misses is.**

---

## 11. Layers, and the line between architecture and implementation

A computer is a stack of layers, each hiding the one below:

```text
Application            your code
Operating system       processes, files, virtual memory
ISA                    <- the contract: instructions, registers, how memory behaves
Microarchitecture      <- how one CPU implements it: pipeline, caches, predictors
Logic gates            AND, OR, NOT, flip-flops
Transistors
```

The **instruction set architecture (ISA)** is the line between software and hardware. It says *what* a program can ask for (which instructions, how many registers, what a memory access means). The **microarchitecture** (also called the *organization*) is *how* one particular chip does it: how deep the pipeline is, how big the caches are, how many instructions run per cycle.

This is why:

- The same x86-64 binary runs on Intel and AMD chips. They implement the same ISA with very different internals.
- Two ARM64 chips (a small embedded core and an Apple M-series core) run the same code at very different speeds.
- Being told "it is an ARM64 CPU" tells you what code it can run, not how fast.

"Architecture" in everyday speech often means the ISA, and "organization" or "microarchitecture" means the implementation. You will hear both words used loosely; ask which one is meant if it matters.

---

## 12. Machines you will meet

| Kind | Typical parts | What is different |
|---|---|---|
| **Laptop or server** | Several x86-64 or ARM64 cores, many GB of DRAM, big caches | Full operating system with virtual memory |
| **Microcontroller** | One small core, kilobytes of SRAM, on-chip flash | Often no virtual memory; may run bare-metal or a small RTOS |
| **System on chip (SoC)** | CPU cores plus a memory controller, flash, Ethernet switch, radios, DMA engines and an interrupt controller, all on one chip | Many of the "devices" are inside the chip |
| **GPU or accelerator** | Hundreds to thousands of simple cores, very high memory bandwidth | Built for many identical operations at once |

A Wi-Fi access point is the third kind: a packet arrives at the radio, a DMA engine copies it into DRAM, an interrupt wakes a CPU core, and software on that core processes it. Tutorials 25 and 26 explain the bus, DMA and interrupt steps.

---

## 13. Misunderstandings that cost points

1. **"The CPU runs C."** It runs machine code. C is translated first, and the translation can look quite different from the source (section 5).
2. **"More GHz means faster."** The iron law says speed also depends on the instruction count and CPI.
3. **"More cores means faster."** Only for work that can be split up (Amdahl's law, tutorial 28).
4. **"Reading memory is cheap."** A DRAM access is hundreds of cycles. Access pattern often matters more than instruction count.
5. **"A variable lives in memory."** At `-O0` it does (on the stack). At `-O1` and above the compiler keeps it in a register when it can (Problem C shows both).
6. **"Same ISA means same speed."** The microarchitecture decides speed.
7. **"Every instruction takes one cycle."** Integer division can take 10 to 40 cycles, and a load can take 4 or 300 depending on where the data is.

---

## 14. Questions you will be asked

**What happens when a CPU executes `x = y + 1`?** The compiler has turned it into instructions. If `x` and `y` are in registers, it is one `add` (or `lea`). The CPU fetches the instruction from the address in the PC, decodes it, reads `y` from its register, the ALU adds 1, the result is written to the register that holds `x`, and the PC advances to the next instruction. If the variables live in memory, it is a load, an add, and a store.

**What is the program counter?** A register holding the address of the next instruction to fetch. It normally advances by the size of one instruction. Jumps, branches, calls and returns set it to a new value, and so do interrupts and system calls.

**Von Neumann versus Harvard?** Von Neumann has one memory for code and data, so a program is just data in memory. Harvard has separate memories, so code and data can be fetched at the same time. Modern CPUs are von Neumann in the address space and split the L1 cache into instruction and data caches.

**What is the difference between the ISA and the microarchitecture?** The ISA is the contract software sees: instructions, registers, memory behaviour. The microarchitecture is how a specific chip implements it. Many different microarchitectures implement the same ISA.

**Why is memory the bottleneck?** A DRAM access is about 70 to 100 ns, which is hundreds of cycles, and CPUs got faster much more quickly than DRAM did. Caches hide this using locality, so the access pattern of a program often decides its speed.

**State the iron law.** Time = instructions × CPI × cycle time. Three independent levers, and a change that improves one may hurt another.

**How does the operating system get the CPU back from a program stuck in a loop?** A timer interrupt. The hardware saves the PC, switches to kernel mode and jumps to the OS's handler, and the scheduler can then run something else.

---

## 15. Problems

**How to use this section.** Try each problem before reading the answer at the bottom. Problems A and B are on paper (15 and 10 minutes). C and D take about 20 minutes each.

### Problem A (paper): trace a program

This toy program multiplies by repeated addition. Memory starts with `memory[100] = 6` and `memory[101] = 4`.

```text
0   LOAD  R1, [100]        R1 = a
1   LOAD  R2, [101]        R2 = b (the counter)
2   LOADI R0, 0            R0 = result
3   LOADI R3, 1
4   ADD   R0, R0, R1       result = result + a
5   SUB   R2, R2, R3       counter = counter - 1
6   JNZ   R2, 4            if counter != 0, go to 4
7   STORE [102], R0
8   HALT
```

(a) What ends up in `memory[102]`, and how many instructions are executed (counting `HALT`)?
(b) What happens if `memory[101]` is 0 to begin with?
(c) How does the compiled `sum` function in section 5 avoid the same problem?

*Where:* paper. *How:* make a table like the one in section 4 with the columns PC, instruction, R0, R2. Do two iterations and the pattern will show; then count the instructions as setup + body × iterations + end.

### Problem B (paper): the iron law

Both machines run the same program of 10⁹ instructions.
Machine A: 2 GHz, CPI 1.5. Machine B: 3 GHz, CPI 2.5.

(a) Which is faster, and by how much?
(b) On machine A, a new compiler removes 20% of the instructions but raises the CPI from 1.5 to 1.8 (the shorter code has more dependencies). Is it a win?

*Where:* paper or a calculator. *How:* time = instructions × CPI / clock rate. Compute everything in seconds and compare the two numbers.

### Problem C (godbolt): read compiler output

*Where:* [godbolt.org](https://godbolt.org). Set the language to C. Paste:

```c
int plus_one(int y) { int x = y + 1; return x; }

int sum(const int *a, int n) {
    int s = 0;
    for (int i = 0; i < n; i++) s += a[i];
    return s;
}
```

Pick an x86-64 gcc compiler and add `-O1` to the options. Then use "Add new > Compiler" to open a second pane with an ARM64 gcc and the same flag.

(a) In `sum`, find the instruction that loads an array element, the one that adds, the compare, and the branch. In which architecture is the load and the add one instruction?
(b) Why does the pointer increase by 4 each time?
(c) Change `-O1` to `-O0` in both panes. How many instructions does `plus_one` take now compared with before, and where did `x` and `y` go?

*How:* ignore everything that starts with a dot or `.cfi`; look only at the instructions between the function label and `ret`. Colour highlighting links each source line to its assembly, so click on the line `s += a[i]`.

### Problem D (your machine): the numbers behind the ladder

*Where:* a terminal on your own computer.

- On Linux: `lscpu` and `lscpu -C` (cache sizes and line size), and `free -h` (memory).
- On a Mac: `sysctl -a | grep -i -E 'cache|cpu|memsize'`. On Apple silicon the names differ from Intel Macs and a clock speed may not be reported; if it is missing, use the chip's published figure.

(a) Write down: number of cores, clock speed, L1 data cache size, L2 size, L3 size (if any), cache line size, total RAM.
(b) Assume a DRAM access takes 80 ns. How many cycles is that at your clock speed? At an IPC of 2, how many instructions' worth of work could the core have done in that time?
(c) How many `int`s fit in one cache line?

*How:* cycles = time × clock rate. Work in the same units (80 ns = 80 × 10⁻⁹ s).

---

## Answers

Try before reading.

### Problem A

(a) The program executes 4 setup instructions (addresses 0 to 3), then the 3-instruction body (`ADD`, `SUB`, `JNZ`) four times, then `STORE` and `HALT`: 4 + 3×4 + 2 = **18 instructions**.

| Iteration | R0 (result) | R2 (counter) | `JNZ` |
|---|---|---|---|
| 1 | 6 | 3 | jump |
| 2 | 12 | 2 | jump |
| 3 | 18 | 1 | jump |
| 4 | 24 | 0 | falls through |

`memory[102] = 24`.

(b) The test is at the bottom, so the body always runs at least once. With `b = 0` the first `SUB` makes the counter -1, which is not zero, so it jumps back. The counter only reaches 0 again after wrapping all the way around a 32-bit register, about 4 billion iterations later. Instead of finishing at once, the program grinds through 4 billion pointless additions (and with a different loop condition the answer would simply be wrong).

(c) `sum` starts with `test esi, esi` / `jle .L5` (ARM64: `cmp w1, 0` / `ble .L5`). That tests `n` before entering the loop and skips it when `n <= 0`. The fix for the toy program is the same: test `b` first and jump over the loop if it is zero.

### Problem B

(a) A: 10⁹ × 1.5 / (2×10⁹) = **0.75 s**. B: 10⁹ × 2.5 / (3×10⁹) = **0.833 s**. The "slower" 2 GHz machine A wins, by about 11%, because its CPI is so much lower. A higher clock does not guarantee a faster machine.

(b) New instruction count = 0.8 × 10⁹. Time = 0.8×10⁹ × 1.8 / (2×10⁹) = **0.72 s**, against 0.75 s before. Yes, a small win (about 4%). The point is that you must multiply the factors, not look at one of them.

### Problem C

(a) On x86-64 at `-O1`, the loop is `add edx, DWORD PTR [rax]` (load and add in one instruction), `add rax, 4`, `cmp rax, rcx`, `jne .L4`. On ARM64 the load is `ldr w1, [x2], 4`, the add is a separate `add w0, w0, w1`, then `cmp x2, x3` and `bne .L4`. ARM64 splits the load from the add because it is a load/store architecture. Your exact registers may differ with the compiler version, but the shape will be the same.

(b) `a` points to `int`s, which are 4 bytes each, so moving to the next element moves the address by 4. (The ARM64 version writes it as the post-increment `[x2], 4`.)

(c) At `-O1`, `plus_one` is a single instruction plus `ret`: `lea eax, [rdi+1]` on x86-64 (using the address-calculation unit as an adder) and `add w0, w0, 1` on ARM64. At `-O0` it grows to 9 instructions on x86-64 (`push rbp`, `mov rbp, rsp`, `mov [rbp-20], edi`, `mov eax, [rbp-20]`, `add eax, 1`, `mov [rbp-4], eax`, `mov eax, [rbp-4]`, `pop rbp`, `ret`) and 8 on ARM64 (your compiler version may differ slightly). The variables `y` and `x` are stored in memory, in the function's stack frame, and loaded again. That is why you never judge performance from an unoptimised build.

### Problem D

Your numbers will differ. As a check, this is what a cloud Linux VM reports (an Intel Xeon): 2 cores at 2.1 GHz; L1 data cache 48 KiB per core; L2 2 MiB per core; L3 260 MiB shared; 64-byte cache lines.

(b) At 2.1 GHz, 80 ns × 2.1×10⁹ = **168 cycles**. At IPC 2 that is about 336 instructions of work. At 3 GHz it would be 240 cycles. The number always comes out in the hundreds.

(c) 64 bytes / 4 bytes per `int` = **16 ints**. The cache brings in all 16 when you touch one, which is why walking an array in order is so much faster than jumping around.

---

## Cheat sheet

- Parts: CPU, memory, storage, I/O, connected by buses. The program lives in memory; the CPU fetches from it.
- Memory is an array of bytes, addressed by number. A pointer is an address held in a variable. Code is stored as numbers beside the data.
- CPU loop: **fetch** (at the PC), **decode**, **execute**, **update the PC**, repeat. A branch is a PC change; loops jump backwards, `if` jumps forwards.
- Four instruction groups: arithmetic and logic, data movement, control flow, system.
- x86-64 `add` can read memory; ARM64 is load/store. ARM64 instructions are always 4 bytes, x86-64 instructions are 1 to 15.
- `syscall` / `svc` enter the kernel; interrupts take the CPU away from the running program.
- **Iron law:** time = instructions × CPI × cycle time. Clock rate alone does not tell you speed.
- DRAM costs hundreds of cycles; caches (64-byte lines) hide it using locality.
- The ISA is the contract; the microarchitecture is the implementation.

**Next:** tutorial 02, Data Representation (bits, two's complement, floats, endianness). Say "next" when you are ready.
