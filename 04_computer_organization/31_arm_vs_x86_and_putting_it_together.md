# CO 31: ARM vs x86, and Putting It Together

*Part of the [Computer Organization plan](00_plan.md) · Core tutorial · Time: about 6 hours (3.5 reading, 2.5 problems) · You need: [tutorial 06](06_isa_design.md) (ISA design), [07](07_assembly_in_practice.md) (assembly on both ISAs), [12](12_inside_a_modern_core.md) (cores), [15](15_virtual_memory_and_the_tlb.md) (virtual memory), [20](20_memory_consistency_and_atomics.md) (memory models) and [26](26_interrupts_exceptions_and_dma.md) (interrupts and system calls); the others are linked where they are used*

By the end of this tutorial you can:

- say what really differs between x86-64 and AArch64 and what does not, with one measurable or checkable consequence for each difference;
- port code between them: find the memory-ordering bugs that x86 hides, and the traps in plain C (the sign of `char`, divide by zero, float to integer, fused multiply-add, alignment, `long double`);
- explain the x86-specific machinery you will meet in a kernel or a hypervisor: privilege rings, model-specific registers, `cpuid`, and the size of the saved vector state;
- answer "ARM or x86?" without saying "RISC against CISC";
- trace eight events from the program down to the hardware, with the cost of each step: running a program, a packet arriving, dereferencing a pointer, power-on to `main`, a system call, a context switch, a page fault, and `malloc` down to DRAM;
- use a cost ledger to say whether a design can meet its per-event time budget.

The first thirty tutorials took the machine apart one piece at a time. This one does the two things interviews do constantly. **Part I** compares the two instruction sets you will be asked about, and adds only what tutorials 06, 07, 12, 20 and 21 did not show: how big real programs are on each, what breaks when you port between them, and what is specific to x86. **Part II** puts the track back together with eight "what happens when..." questions. Interviewers rarely ask "what is a TLB"; they ask "what happens when I run `./a.out`" and listen for whether you reach the TLB without being asked.

**Where the numbers come from.** The virtual machine of tutorials 14 to 30 (two vCPUs of an Intel Xeon, family 6 model 0xcf, under KVM, Linux 6.18, GCC 13.3). **I have no Arm hardware here.** Arm code was compiled with Ubuntu's cross compiler (`aarch64-linux-gnu-gcc` 13.3), disassembled, and *run* under QEMU's user-mode emulation (`qemu-aarch64`). QEMU executes the instructions correctly, so it shows what Arm code **computes and how big it is**; it says nothing about **how fast an Arm core runs it**, and on an x86 host it does not reorder memory accesses the way Arm hardware may (section 3). So every Arm number below is about code size and behaviour, never speed. **Measured here, on x86:** `cpuid` and the saved vector state (section 5), the cost of a context switch, page faults, `fork` and `exec`, `malloc`, the interrupts and receive buffers of a download, and the boot log of this VM (Part II). **Measured here, on both ISAs, by compiling and running:** the size and instruction mix of the same 53,000-line C file (section 2), the generated code for atomics (section 3), and a program whose results differ between the ISAs in eight of its nine tests, and in the ninth on an older Arm core (section 4). **Not measured:** the speed of any Arm core, Arm memory-ordering behaviour, a real network card, real firmware. **Published:** the Linux kernel documentation on [arm64 memory layout](https://docs.kernel.org/arch/arm64/memory.html) and on the [Scalable Vector Extension](https://docs.kernel.org/arch/arm64/sve.html), and Blem, Menon and Sankaralingam, [Power Struggles](https://class.ece.iastate.edu/tyagi/cpre581/papers/HPCA13powerstruggles.pdf) (HPCA 2013), whose abstract I read through a summary tool rather than the full paper. Numbers quoted from other tutorials carry their tutorial number. Details I know but did not check against a manual are marked "standard": the x86 reset vector, the Trusted Firmware stage names, the exception class of `svc`, which components Linux saves lazily. **I wrote no exploit.**

> **The one-sentence model.** x86-64 and AArch64 differ in the encoding, the register count, the memory-ordering rules and the privileged machinery, and agree on nearly everything that decides speed (out-of-order cores, caches, translation, DRAM), so porting is a matter of ordering, ABI details and behaviour that C leaves undefined; and every event on a computer, from `exec` to a DRAM read, is the same chain (a request, a translation, a cache, a kernel path, maybe a device), whose cost you can read off a ledger.

---

## Part I: ARM against x86

## 1. The map

| Area | x86-64 | AArch64 | What it changes for you | See |
|---|---|---|---|---|
| Encoding | 1 to 15 bytes | 4 bytes, always | decode width; code size (section 2) | 06, 12 |
| General registers | 16 | 31 (`x0` to `x30`), plus `sp` and a zero register | fewer spills on Arm (section 2) | 07 |
| ALU operands | two-operand (destination is also a source), memory operands allowed | three-operand, load/store only | x86 needs register copies; Arm needs explicit loads | 06 |
| Flags | almost every ALU instruction sets them | only instructions with an `s` suffix (`adds`, `cmp`) | compare and branch can be far apart; `csel` and `ccmp` | 07 |
| Calling convention (Linux) | `rdi rsi rdx rcx r8 r9`; return address on the stack; 128-byte red zone | `x0` to `x7`; return address in `x30`; no red zone | leaf functions on Arm touch no stack | 07 |
| Memory model | TSO: only a store may pass a later load | weak: almost everything may be reordered | missing `release`/`acquire` hides on x86 (section 3) | 20 |
| Atomics | `lock` prefix, `cmpxchg` | load-exclusive/store-exclusive, and from Armv8.1 single-instruction atomics (`ldadd`, `cas`) | `compare_exchange_weak` exists for the first kind | 20 |
| SIMD | SSE, AVX, AVX-512 (fixed width) | NEON (128 bits), SVE (length-agnostic) | one SVE binary runs at any vector length | 21 |
| Privilege | rings 0 to 3 (Linux uses 0 and 3); VMX root and non-root | EL0 to EL3 | firmware at EL3, hypervisor at EL2 | 26, 27 |
| System call | `syscall`, number in `rax` | `svc #0`, number in `x8` | section 12 | 26 |
| Translation roots | one root register (`CR3`) for user and kernel | two: `TTBR0_EL1` (low addresses), `TTBR1_EL1` (high), selected by address bit 55 | Arm switches the user half without touching the kernel half | 15 |
| TLB tag, shootdown | PCID (12 bits); IPI plus `invlpg` | ASID (8 or 16 bits); broadcast `TLBI` plus `DSB` | no interrupt for a shootdown on Arm | 15 |
| Instruction and data caches | coherent: code you just wrote can run | **not** coherent for code you just wrote: clean the data cache, invalidate the instruction cache | JIT compilers need explicit maintenance on Arm | 14 |
| Interrupt controller | local APIC, I/O APIC, MSI | GIC | | 26 |
| Boot | reset vector, firmware (UEFI or BIOS), ACPI | boot ROM, trusted firmware at EL3, UEFI or U-Boot, ACPI or a device tree | section 11 | 30 |
| Hardware security | NX, SMEP/SMAP, CET | XN/PXN, PAN, pointer authentication, BTI, memory tagging | | 30 |

**What is the same**, and carries most of the performance story: both are 64-bit and little-endian in practice, use 48-bit virtual addresses with four-level page tables for 4 KB pages (the kernel documentation says so for Arm), have out-of-order superscalar cores that crack instructions into smaller internal operations, keep their caches coherent in hardware, and follow IEEE 754 for floating point. The `long` and pointer sizes are 8 bytes on both under Linux. Tutorial 06 put it plainly: the labels RISC and CISC blurred, and the lasting differences are decode complexity, the memory-ordering model, the software ecosystem and licensing.

The instruction-and-data-cache row is a real porting trap that nobody mentions until a JIT crashes. I checked what the compiler does with `__builtin___clear_cache(a, b)`: on x86-64 GCC emitted just `ret` (nothing to do), on AArch64 it emitted a call to `__clear_cache`, which does the maintenance. Code that writes instructions into memory (a JIT, a dynamic linker's trampolines, a debugger inserting breakpoints) is correct on x86 without the call and **wrong on Arm without it**.

---

## 2. Code density, measured on a whole program

Tutorial 06 compared one small loop on three ISAs and found x86 denser (25 bytes against 48 for AArch64), and said not to generalise. Here is a large program instead. I compiled the same C source, **zstd** (the amalgamated 53,692-line file from the `zstandard` source package) with the same compiler (GCC 13.3) at `-O2` and `-Os`, for baseline x86-64 and baseline Armv8-A, with stack protector, CET and branch-protection turned off so that neither side carries extra instructions. Then I counted the `.text` section instruction by instruction (`density.py`, Problem B; padding `nop`s counted separately).

| `.text` of zstd | x86-64 | AArch64 | AArch64 / x86-64 |
|---|---:|---:|---:|
| `-O2`: instructions | 141,447 | 120,507 | 0.85 |
| `-O2`: bytes | 567,576 | 482,028 | 0.85 |
| `-O2`: padding `nop`s (not counted above) | 5,322 (29,335 bytes) | 1,031 (4,124 bytes) | |
| `-Os`: instructions | 96,342 | 82,513 | 0.86 |
| `-Os`: bytes | 367,234 | 330,052 | 0.90 |
| average bytes per instruction, `-O2` | 4.01 | 4.00 | |
| instructions that access memory, `-O2` | 48,940 (34.6 %) | 32,614 (27.1 %) | 0.67 |
| of those, stack accesses (`push`, `pop`, `rsp`/`rbp` operands; `sp`/`x29` operands) | 28,159 (19.9 %) | 12,658 (10.5 %) | 0.45 |

```text
x86-64 instruction lengths, -O2:  1 byte 1.9 %, 2 bytes 13.5 %, 3 bytes 30.0 %, 4 bytes 19.9 %, 5 bytes 19.4 %, 6 bytes 7.6 %, 7 bytes 3.5 %, 8 bytes 3.1 %
most common mnemonics, -O2:  x86-64  mov 34.0 %, cmp 8.9 %, add 6.9 %, lea 5.5 %, sub 4.5 %, jmp 3.9 %
                             AArch64 mov 12.9 %, add 12.8 %, ldr 11.6 %, cmp 10.1 %, sub 5.9 %, str 5.7 %, b 4.1 %, ldp 3.1 %
```

What this shows:

1. **The variable-length encoding gave no density advantage here.** The average x86 instruction is 4.01 bytes, equal to the fixed 4.00 of AArch64. Only 15 % of x86 instructions are 1 or 2 bytes long; the bulk are 3 to 5 bytes, because a REX prefix, a 32-bit displacement or a 32-bit immediate adds bytes quickly. The result in tutorial 06 (x86 smaller) came from one tiny loop; a whole program went the other way. **One function measures that function.**
2. **x86 needed 17 % more instructions** (141,447 against 120,507). The mnemonic mix says where: `mov` is 34 % of x86 instructions against 13 % on AArch64 (the x86 `mov` covers register copies, loads and stores, so compare it with `mov` plus `ldr` plus `str`: 30 %); two-operand arithmetic means `c = a + b` often needs a copy first; and **x86 makes 2.2 times as many stack accesses** (28,159 against 12,658) with 16 registers against 31. An AArch64 `ldp` or `stp` moves two registers at once (5.7 % of its instructions), so per register moved the gap is smaller than 2.2, but not gone.
3. **Fewer instructions is not faster.** This is a static count of one program. How fast a core runs it depends on the micro-architecture: tutorial 12 shows that wide Arm cores decode ten fixed-size instructions per cycle, and x86 cores cache already decoded micro-ops, and both end up feeding a similar back end. What the numbers do say is that the *front-end* argument for x86's variable length ("denser code, better instruction cache use") is not supported by this program.

**Caveats, so you do not over-quote this.** One program, mostly integer and bit manipulation. One compiler version. Baseline ISAs: the x86 build uses SSE2 only, the Arm build Armv8.0 without SVE or atomics extensions; x86 with AVX2 or AArch64 with SVE would change the mix. Jump tables and constant pools live in `.rodata`, which I did not count. The micro-operation count (what the core really executes) is not visible from the object code: an x86 `add [mem], reg` is one instruction and several micro-ops (tutorial 06, section 8), and some Arm instructions (`ldp`, a post-indexed load with write-back) are cracked too. I could not count micro-ops without performance counters, and this VM has none.

---

## 3. Memory ordering and atomics: the porting view

Tutorial 20 explains the models, the litmus tests and the instruction table. What a porter needs is a procedure, because the bug is **invisible** on the machine you develop on.

**What the compiler turns your atomics into.** I compiled the two halves of a message-passing hand-off (`data = 42; flag.store(1, ORDER)` and `while (!flag.load(ORDER)) {} return data;`) with `ORDER` relaxed and then release/acquire, plus `fetch_add`, with GCC 13.3 `-O2` for both ISAs. The key instruction is the only thing that changes:

| C++ | x86-64 | AArch64 (default flags) | AArch64 `-march=armv8.1-a` | AArch64 `-mno-outline-atomics` |
|---|---|---|---|---|
| `flag.store(1, relaxed)` | `mov` | `str` | `str` | `str` |
| `flag.store(1, release)` | `mov` (**identical**) | **`stlr`** | `stlr` | `stlr` |
| `flag.load(relaxed)` | `mov` | `ldr` | `ldr` | `ldr` |
| `flag.load(acquire)` | `mov` (**identical**) | **`ldar`** | `ldar` | `ldar` |
| `counter.fetch_add(1, relaxed)` | `lock xadd` | `bl __aarch64_ldadd4_relax` | `ldadd` | `ldxr` ; `add` ; `stxr` ; `cbnz` loop |
| `counter.fetch_add(1)`, seq_cst | `lock xadd` | `bl __aarch64_ldadd4_acq_rel` | `ldaddal` | `ldaxr` ; `add` ; `stlxr` loop |

Two things to take from it. **First**, on x86 the relaxed and the release/acquire versions are the *same machine code*; the ordering you forgot to write cannot show. **Second**, the default Ubuntu AArch64 build calls a small library routine (`__aarch64_ldadd4_*`, "outline atomics") that picks the single-instruction form on a CPU that has it and the load-exclusive loop on one that does not; so the same binary is correct everywhere, and you pay a call for the portability. Building with `-march=armv8.1-a` or later emits `ldadd` directly and only runs on cores with that extension.

**Why a test on x86 proves nothing.** The message-passing test (`mp.cpp`, Problem A): thread A stores `data` and then `flag`; thread B waits for `flag` and then reads `data`. The forbidden outcome is "flag seen, data still 0". With only relaxed atomics (and a compiler-only fence so that the code order is kept):

```text
x86-64, native, relaxed (3 runs)          : 0 of 2,000,000 rounds saw the flag set but the data still 0
x86-64, native, release/acquire           : 0 of 2,000,000
AArch64 under QEMU on this x86 host, relaxed          : 0 of 2,000,000
AArch64 under QEMU on this x86 host, release/acquire  : 0 of 2,000,000
```

All zeros, in the version that is **wrong on Arm hardware** and in the version that is right. x86 forbids this reordering in hardware (tutorial 20, section 3.4), and QEMU's translation on an x86 host does not reorder the guest's loads and stores (standard), so the emulation inherits the strong ordering. I could not make the bug appear here, and **the lesson is that I could not**: a lock-free structure that passed every test on an x86 laptop has not been tested for ordering at all.

**A porting procedure.**

1. **Find every synchronisation by hand**, not by test: atomics with `relaxed`, plain `bool` flags, `volatile` used as a synchronisation device (it is not one, tutorial 20 section 5.4), hand-written spinlocks, double-checked locking, lock-free queues. For each pair "write data, then publish" and "see publication, then read data", the publish needs `release` and the read needs `acquire` (or a lock).
2. **Run ThreadSanitizer on x86** (`-fsanitize=thread`). It checks the C++ happens-before relation, not the hardware's, so it reports the missing `release`/`acquire` even though the hardware hides it. This is the cheapest tool that catches the bug on the machine you have.
3. **Look at the generated code** (`objdump -d`) for `ldar`/`stlr`/`dmb` where you expect them, and for the absence of `dmb` in code you thought needed none.
4. **Test on the weakest hardware you ship**, under load, for hours. For the core primitive, model-check the litmus shape (herd7-style tools; standard).
5. **Do not "fix" it with a full barrier everywhere.** `dmb ish` after every store makes it correct and slow; the pair `stlr`/`ldar`, or a lock, is the right size.

The LL/SC side (`compare_exchange_weak`, why the store-exclusive can fail spuriously, why LSE scales better under contention) is in tutorial 20, section 6.3.

---

## 4. Porting pitfalls, run on both ISAs

C leaves many things undefined or implementation-defined, and each ISA fills the gap its own way. Code that "works" on x86 is often relying on x86's choice. I wrote one program (`pitfalls.c`, 49 lines, Problem B) that prints the result of each, compiled it for both, and ran the x86 build natively and the AArch64 build under QEMU. QEMU implements the architecture's rules for these instructions, so the results show the architecture, not the emulator's mood.

```text
                                           x86-64 (native)                  AArch64 (QEMU, -cpu max)
char c = 200;  c < 0                       1   (char is signed)             0   (char is unsigned)
sizeof(long double), mantissa bits         16 bytes, 64 bits (x87)          16 bytes, 113 bits (IEEE binary128)
7 / 0  (int)                               SIGFPE                           0
INT_MIN / -1                               SIGFPE                           -2147483648
(int) NaN                                  -2147483648                      0
(int) 1e10                                 -2147483648                      2147483647
(unsigned) -1.0                            4294967295                       0
a*a - b, a = 1+2^-30, b = 1+2^-29         0                                8.67362e-19  (= 2^-60)
atomic add at an address that is 2 mod 4   worked                           worked with -cpu max; SIGBUS with -cpu cortex-a57
```

Reading the rows:

- **Plain `char` is signed on x86 Linux and unsigned on AArch64 Linux.** The classic bug is `char c; while ((c = getchar()) != EOF)`: on Arm `c` can never equal -1, so the loop never ends. Use `int` for `getchar`, and `signed char` or `int8_t` when the sign matters. (GCC's `-fsigned-char` papers over it; fixing the type is better.)
- **`long double`** is the 80-bit x87 format on x86 and a software-emulated 128-bit format on AArch64. Same size, different values and a large speed difference. Do not use it for data that crosses machines.
- **Integer division by zero traps on x86 and returns 0 on AArch64** (`sdiv` does not trap). `INT_MIN / -1` traps on x86 and wraps on Arm. A program that relies on the `SIGFPE` handler to catch bad input silently continues on Arm. Check the divisor in the code.
- **Float to integer conversion out of range is undefined in C**, and the hardware conversions differ: x86's `cvttsd2si` returns the "integer indefinite" value `0x80000000` for NaN and for overflow; Arm's `fcvtzs` saturates and turns NaN into 0. Code that tests `(int)x == INT_MIN` to detect overflow is x86-only.
- **Fused multiply-add changes results.** On AArch64 `fmadd` and friends are baseline, and GCC contracts `a*b - c` into one fused operation at `-O2` (`-ffp-contract=fast` is its default for GNU C); on baseline x86-64 there is no FMA, so the product is rounded first. Here the same expression gives 0 on x86 and 2^-60 on Arm. With `-ffp-contract=off` the Arm build also printed 0; with `-mfma` the x86 build printed 8.67362e-19. At `-O1` the Arm build did not fuse either, so this is also a trap across optimisation levels. Numerical code that must match bit for bit across machines needs `-ffp-contract=off` (and the same libm).
- **Atomics need alignment on Arm.** A load-exclusive/store-exclusive pair on a misaligned address raises an alignment fault (`SIGBUS`); QEMU's `cortex-a57` model (Armv8.0, no large-system extension) did exactly that, while `-cpu max` did not, which fits the newer Armv8.4 rule (LSE2, standard) that allows a misaligned atomic that stays inside a 16-byte granule. On x86 a misaligned `lock` operation inside one cache line just works; one that **straddles two cache lines** works too but is a "split lock" that stalls the whole memory system (standard; Linux can be told to detect and slow or kill such programs). The portable rule: keep atomic objects naturally aligned, and never take the address of a packed-struct member for an atomic.
- **Not in the table**, because compile-time behaviour is fixed by the ABI: the red zone (x86-64 System V gives a leaf function 128 bytes below `rsp`; AArch64 does not, tutorial 07), `va_list` layouts, and the size of `max_align_t`. Hand-written assembly and inline `asm` do not port at all; use compiler builtins and the intrinsics headers (`immintrin.h`, `arm_neon.h`).

A last item that does not need an experiment: the page size. x86-64 Linux always uses 4 KB base pages. Arm hardware defines 4 KB, 16 KB and 64 KB translation granules (standard), and Linux can be built for each; the kernel documentation covers 4 KB and 64 KB. Code that assumes `4096` instead of calling `sysconf(_SC_PAGESIZE)` breaks on a 16 KB or 64 KB kernel, and `mmap` offsets, alignment of DMA buffers and huge-page sizes all derive from it.

---

## 5. What is specific to x86

### 5.1 Rings, model-specific registers, and what user code cannot do

x86 has four privilege rings; Linux uses ring 0 (kernel) and ring 3 (user), and VMX adds a root and a non-root mode on top (tutorial 27). Instructions that touch machine state are **privileged**: `rdmsr` and `wrmsr` (the **model-specific registers**, hundreds of per-CPU control and status registers such as the system-call entry address `IA32_LSTAR` and the speculation controls of tutorial 30), `lgdt`, `lidt`, writes to `CR0` to `CR4`, `hlt`, and port I/O. Executed in ring 3 each raises `#GP`, which Linux delivers as `SIGSEGV` (tutorial 26, section 1). In this VM `/dev/cpu/0/` contains only `cpuid` (there is no `msr` device), so even root has no user-space route to the MSRs; only the kernel writes them.

### 5.2 `cpuid`: the CPU describing itself

`cpuid` is an unprivileged instruction that returns, for a "leaf" number in `eax`, a block of fields: vendor, family/model/stepping, feature bits, cache and topology descriptions, and the size of the saved state. It is how libraries choose between AVX2 and AVX-512 versions of a function at run time (tutorial 21, section 8) and how glibc's `ifunc` picks `memcpy`. In a VM it always exits to the hypervisor (7.5 µs here, tutorial 26), so never call it in a hot path. This VM reports, from `xstate.c` (Problem B):

```text
vendor GenuineIntel, highest basic leaf 0x20
family 6 model 0xcf stepping 2, hypervisor bit 1
hypervisor leaf 0x40000000: "KVMKVMKVM"
```

The hypervisor bit and the `KVMKVMKVM` signature are how a guest knows it is a guest (tutorial 27). The Arm counterpart is a set of read-only system registers (`MIDR_EL1` for the implementer and part number, `ID_AA64*` for features) that Linux exposes through `/proc/cpuinfo` and the auxiliary vector (standard).

### 5.3 The saved vector state

Everything a context switch or a signal must save is the **architectural state**, and on x86 it has grown with every vector extension. `cpuid` leaf 0xD lists the components, their sizes and where they sit in the `XSAVE` area:

```text
XSAVE: features enabled in XCR0 = 0x600e7; area for them = 11008 bytes
  component  2  AVX (ymm high halves)              256 bytes at offset 576
  component  5  AVX-512 opmask k0-k7                64 bytes at offset 1088
  component  6  AVX-512 zmm0-15 high 256 bits      512 bytes at offset 1152
  component  7  AVX-512 zmm16-31                  1024 bytes at offset 1664
  component 17  AMX tilecfg                         64 bytes at offset 2752
  component 18  AMX tiledata                      8192 bytes at offset 2816
minimum signal stack: sysconf(_SC_MINSIGSTKSZ) = 11952, _SC_SIGSTKSZ = 47808
```

The first 576 bytes hold the legacy x87 and SSE state and the header; AVX-512 brings the total to about 2.7 KB; the **AMX tile registers** (the matrix unit of tutorial 24, 8 KB of tile data) bring it to **11,008 bytes**, because this CPU and kernel enable them. Three consequences: the kernel's context-switch and signal code moves up to that much per task (it can skip components that are still in their initial state with the `XSAVEOPT` and `XSAVEC` forms, standard; the `xsaveopt`, `xsavec` and `xsaves` flags are present here); the **signal frame** includes it, so the minimum signal stack that glibc reports is 11,952 bytes and the recommended one 47,808 bytes, far above the old fixed 2 KB and 8 KB constants, and a program that allocates its own alternate signal stack with an old constant can overflow it; and an AMX-using program makes every context switch and signal more expensive.

Arm's equivalent: SVE state is `32 × VL + 17 × VL/8` bytes plus a few control registers (the kernel documentation: about 34 times the vector length in bytes), so 2,184 bytes at 512-bit vectors and 8,736 bytes at the 2048-bit maximum, and a process can ask for a smaller vector length with `prctl(PR_SVE_SET_VL, ...)` (the length range is 16 to 256 bytes in steps of 16). The one-binary-many-widths property of tutorial 21 is paid for in a state size that depends on the machine.

---

## 6. "ARM or x86?" without saying RISC against CISC

The honest published evidence is old but clear. Blem, Menon and Sankaralingam ran mobile, desktop and server workloads on four real chips (ARM Cortex-A8 and Cortex-A9, Intel Atom and Core i7 "Sandy Bridge", 2013) and concluded that whether an ISA is RISC or CISC appears to be irrelevant: the chips are design points tuned for different performance levels, and **micro-architecture, not instruction set, drives the differences in performance, power and energy**, at performance levels from the Cortex-A8 upwards. Tutorial 06 and tutorial 12 say the same from the other direction. The cores have changed since 2013; the argument has not.

So a good answer separates four things.

| Question | What decides it | Evidence in this track |
|---|---|---|
| Is one *faster*? | the core design: width, caches, predictors, clock, memory system | tutorials 10 to 14, 28; section 2 here (the density numbers do not predict speed) |
| Is one *more efficient*? | the same design choices plus the process node and the target frequency; **not** the ISA label | Blem et al.; tutorial 28 |
| What does the ISA *cost to build*? | decode: easy to widen with fixed 4-byte instructions, harder with 1 to 15 bytes (x86 compensates with the micro-op cache); legacy modes and the weak/strong memory model | tutorial 12 |
| What does it cost the *software*? | memory ordering, the ABI, undefined behaviour, the assembly and intrinsics you wrote, JIT cache maintenance, the ecosystem and licensing (Arm licenses the ISA and sometimes the core design; x86 has two vendors) | sections 3 and 4 |

For your own work the deciding facts are usually these. The product line decides (an access point is an Arm or MIPS system on a chip; a cloud server can be either, and an Arm instance may be cheaper per core). The **software you must port** decides (a codebase with hand-written AVX intrinsics and lock-free code tested only on x86 is the expensive port). **Latency-critical tails** care about the details of one core, and you measure on the target. If you have measurements from your own Arm hardware, they beat any general claim: the older version of this note recorded, from a machine of yours, a false-sharing penalty of 6.9 times on Arm against 5.0 times on a Xeon and a huge-page benefit of 4.4 times against about 1.2 times at 16 MiB. I did not re-run those here; treat them as a reason to re-measure the things that depend on the memory system (page size, cache-line size, which can be 128 bytes on Apple's cores) before porting a tuned data structure.

**Smaller differences that bite in drivers and timing code** (standard knowledge, not tested here). Unaligned access is fine in normal memory on both, but Arm faults on it in *device* memory, so kernel code uses the `get_unaligned` helpers instead of casting. Both are little-endian in practice (AArch64 can be big-endian and effectively never is), so network byte-order conversions are still needed. The descriptor-then-doorbell ordering of tutorial 26 section 7.4 (`dma_wmb()`, `writel()`) is a real barrier on Arm and nothing on x86. The cycle counter is `rdtsc` on x86 and `CNTVCT_EL0` on Arm (tutorial 26 section 6.3), and the Arm performance-monitor counters usually need the kernel's permission before user space can read them.


---

## Part II: Putting it together

## 7. How to answer "what happens when...", and the cost ledger

An interviewer who asks "what happens when I run `./a.out`" is not asking for the ELF specification. They are checking three things: that you can name the **layers** in order (shell, kernel, loader, hardware), that you reach the **hardware mechanisms** without being prompted (page tables, TLB, caches, interrupts), and that you know **roughly what each step costs**, so you can say where the time goes. A reliable shape for every answer in this part:

1. **Trigger**: what starts it (an instruction, a signal on a wire, a timer).
2. **Path**: the ordered steps, each naming the mechanism (the tutorial where you learned it).
3. **Cost**: a number for the expensive steps, and which one dominates.
4. **Failure**: the one or two ways it goes wrong in production.

The numbers are the part candidates skip. Here is the ledger the eight walkthroughs use. "This" means measured in this VM for this tutorial; the others come from the tutorial named. It is one machine and one session: the orders of magnitude are the lesson, the exact figures are not.

| Event | Time | Source |
|---|---:|---|
| L1 hit | 0.4 to 1.3 ns | 14, 29 |
| A cache line from another core's cache | 56 to 102 ns | 19, 26 |
| DRAM miss (221 ns when it also misses the TLB) | 146 to 149 ns | 14, 29 |
| `rdtsc` | 16 ns | 26 |
| `clock_gettime` through the vDSO | 29 to 33 ns | 26 |
| raw system call (`getppid`) | 115 to 126 ns (about 262 TSC ticks) | 26 |
| `malloc(64)` + `free`, thread cache hit | 8 to 15 ns | this |
| first write to a fresh 4 KB page (minor fault) | 1.1 to 1.3 µs (1.5 to 2.5 µs in tutorial 15's session) | this, 15 |
| a context switch, one way (pipe hand-off, one CPU) | 1.2 to 1.5 µs | this (26: 1.6 to 1.8 µs by futex) |
| a fault delivered to a signal handler and back | 1.5 to 1.7 µs | 26 |
| `mmap` 1 MiB, touch one page, `munmap` | 3.5 to 6.4 µs | this |
| TCP loopback, 8 bytes, one way (same CPU, other CPU) | 5.3 µs, 16.8 µs | 29 |
| `cpuid` (exits to the hypervisor) | 6.9 to 7.8 µs | 26 |
| waking a sleeping thread on another CPU | about 10 µs | 26 |
| first touch of a 2 MB huge page | 280 to 370 µs | 15 |
| `fork` + `exec` + `wait` of a trivial program | 0.8 to 1.2 ms | this |
| a major fault that reads 4 MiB from the virtual disk | 1.4 to 3.7 ms | this |
| light in 1 km of fibre | 4.9 µs | 29 |

Each rung is about ten times the one above. When you trace an event, find the rung it sits on and say which of its steps is the biggest.

---

## 8. Walkthrough 1: running a program

**Trigger.** You type `./hello` (a dynamically linked PIE that prints one line).

**Path.**

1. **The shell calls `fork`.** The kernel copies the shell's page tables, not its memory, and marks private pages copy-on-write (tutorial 15, section 8; about 50 to 60 ns per resident page). The child immediately calls `execve`, so the copy was wasted; `posix_spawn` and `vfork` avoid it.
2. **`execve` replaces the address space.** The kernel opens the file, sees the ELF magic, throws away the child's mappings (freeing page tables and flushing the TLB), creates a new address space and maps each loadable segment from the file **without reading it** (the pages come in on demand). It lays out the stack with `argv`, `envp` and the **auxiliary vector** (entry point, program headers, a random seed, the CPU's feature bits), picks random bases (ASLR, tutorial 30 section 6.2), and, because the file names an interpreter (`PT_INTERP`: `/lib64/ld-linux-x86-64.so.2`), maps that too and starts there.
3. **The first instruction fetch faults.** Nothing is mapped yet: a page fault on a file-backed page (tutorial 15), a page-cache lookup, a page-table entry, then the instruction restarts. The instruction cache and the instruction TLB are cold.
4. **The dynamic loader runs.** `strace` shows it: it opens `/etc/ld.so.cache`, opens `libc.so.6`, reads its header, **reserves** the library's whole address range and maps its segments over it (read and execute, read-only, read-write, plus an anonymous area for `.bss`), sets the thread pointer with `arch_prctl(ARCH_SET_FS)` (the register that makes `%fs:0x28`, the stack canary of tutorial 30, work), makes the relocation table read-only with `mprotect`, and processes relocations. glibc's `ifunc` mechanism picks the version of `memcpy`, `strlen` and friends that matches this CPU's `cpuid` flags (section 5.2; standard, I did not trace that step). `LD_DEBUG=statistics` in six runs reported 96 relocations and 115,000 to 190,000 time-stamp-counter cycles (55 to 90 µs at 2.1 GHz) in the loader, of which processing relocations was 0.8 to 1.0 % and loading the objects 5.2 to 6.7 %; that report does not break down the rest.
5. **`_start` runs**, then `__libc_start_main`, the constructors, and `main` (the entry point of this PIE is the offset `0x1070` in the file, `readelf -h`). `puts` fills a buffer; at exit it is flushed with `write(1, "hello\n", 6)` (walkthrough 5).
6. **`exit_group`** tears the address space down (every mapping removed, every page freed), sends `SIGCHLD`, and the shell's `wait` returns.

**Cost, measured.** A loop of 500 `fork` + `execve` + `wait` cycles, three runs each, output to `/dev/null`:

| Program | Time per run | Minor page faults per run | System calls (`strace`, including `execve` and `exit_group`) |
|---|---:|---:|---:|
| `hello` linked statically (785 KB) | 784 to 932 µs | 46.5 | 17 |
| `hello` linked dynamically (16 KB) | 922 to 1,100 µs | 75.4 to 75.5 | 35 |
| `/bin/true` (dynamic) | 888 to 1,192 µs | 71.5 to 72.5 | |

About **a millisecond** to run a program that does nothing, in this VM. The page faults (75 at about 1.2 µs) explain roughly a tenth of it; the rest is process creation and destruction, waking the child on another CPU (an inter-processor interrupt, about 10 µs in this VM), and the VM's own overheads. I did not break it down further: there is no profiler here. Static linking removes 18 system calls and 29 page faults and saved 110 to 180 µs per run in the three pairs of runs; that is worth it for a tool you start thousands of times, and not for a daemon.

**Where it goes wrong.** A shell pipeline that spawns one process per input line is dominated by exec. Cold starts after a deploy are slow because every page of the binary and its libraries faults in on first use (pre-fault or warm up). A large resident parent makes `fork` slow in proportion to its pages (tutorial 15).

**On Arm.** The same flow with `ld-linux-aarch64.so.1`, `TPIDR_EL0` in place of `%fs`, and the page size of that kernel (4, 16 or 64 KB) deciding how many faults a given binary takes.

---

## 9. Walkthrough 2: a packet arrives

**Trigger.** A frame reaches the network card. Tutorial 26 section 8.2 gives the full nine-step path (descriptor fetch, DMA write, completion write, interrupt, top half, NAPI poll, protocol processing, socket queue and wake-up, the copy). In one line: **DMA into a pre-posted buffer, then one interrupt, then a software poll that hands a batch to the stack, then a wake-up of the thread blocked in `recv`.**

**What this VM adds, measured.** The VM's `eth0` is a `virtio-net` device whose receive and transmit queues use their own MSI-X vectors (`virtio7-input.0` and `virtio7-output.0` in `/proc/interrupts`). I read the packet counters, the interrupt counts of those two vectors and the `NET_RX` softirq count before and after downloading a 711,513-byte file through the sandbox's proxy, three times, and over 0.4 s of idle as a baseline:

```text
                          rx packets   bytes per packet   rx interrupts   NET_RX softirq runs
download, run 1                  106            6,850              63              126
download, run 2                  113            6,424              67              132
download, run 3                  123            5,920              72              137
0.4 s of idle                      8               66               3                6
```

Reading it:

- **A "packet" in `/proc/net/dev` is 5.9 to 7.3 KB here (four runs), not 1,400 bytes.** The interface MTU is 1,400, so the file is more than 500 wire-sized frames' worth of payload; the guest was handed about 110 packets instead. The device negotiated `GUEST_TSO` and `MRG_RXBUF` (tutorial 26 section 8.6), so the host side may pass larger-than-MTU packets, and receive aggregation (tutorial 26 section 8.4) is the usual reason; I did not look inside the host to confirm which. One trip through the stack then handles four to five frames' worth, which is what makes high rates affordable in software.
- **About 1.7 packets per interrupt, and about 2 `NET_RX` runs per interrupt.** At this low rate (110 packets in 0.6 to 1.5 s) NAPI has little to batch: nearly every arrival interrupts. The transmit vector fires as often as the receive one (63 receive and 67 transmit interrupts in run 1: the acknowledgements and requests going the other way), and the idle line shows background traffic of about 20 packets a second. At a high rate the interrupt is masked while the poll loop drains the ring, and the ratio rises sharply (tutorial 26 section 4).
- **What the guest cannot see:** the physical NIC, the host's own receive path, and the virtual switch. Delivering each interrupt to a guest involves the hypervisor (tutorial 27); I did not measure that part.

**Cost.** Receive-side work per packet has three parts: the interrupt entry and top half (microseconds, more in a VM; not measured separately), the poll and protocol processing (about a microsecond per packet, *assumed, not measured*), and the **wake-up of the reader**, which is 60 ns if it spins and about 10 µs if it sleeps on another CPU in this VM (tutorial 26). The arithmetic of whether a design can keep up is Problem D.

**Where it goes wrong.** One interrupt per packet at a high rate starves everything else (receive livelock: moderation and NAPI exist for this). All of a flow's packets hash to one queue and one core (the hash is per flow, not per packet). The copy into the application's buffer and a sleeping receiver dominate small-message latency. A kernel-bypass stack removes the interrupt, the copy and the wake-up, at the price of a core spinning.

**On Arm.** Message-signalled interrupts are routed by the GIC (through its interrupt translation service, standard) instead of the APIC; NAPI is identical. Many Arm SoCs have DMA that is **not** cache-coherent with the CPU, so the driver must clean and invalidate the cache around each buffer (the DMA-mapping API does it; tutorial 26 section 7), where x86 gets it for free.

---

## 10. Walkthrough 3: dereferencing a pointer

**Trigger.** `int v = *p;` becomes one load instruction. Tutorial 15 section 12 draws the full decision tree; this is it in order, with what each step costs.

1. **Address generation** (base plus index times scale plus displacement) in an address-generation unit; the load enters the load queue (tutorial 12).
2. **Translation.** The L1 data TLB is looked up in parallel with the L1 cache's set index (the cache is indexed by the page offset, which needs no translation). A hit gives the physical page number at once. A miss goes to the second-level TLB (about 7 cycles more). A miss there starts a **page walk**: four dependent memory reads on x86-64 (up to 24 in a VM, because every guest-physical address in the walk is itself translated; tutorial 27), helped by the walk caches, with the page-table entries fetched through the data caches. In this VM the extra cost was 45 to 75 ns (tutorial 15 section 6.3).
3. **Permission check.** Present? Readable? User allowed? If not, a **page fault** (walkthrough 7) and the instruction restarts later.
4. **L1 tag compare** with the physical tag: a hit returns in about 5 cycles. A miss allocates a miss-status register and goes to L2 (about 20 cycles, 5 ns on this VM's core), then L3 (about 55 ns here), or to the other core's cache if that core holds the line (75 to 102 ns, tutorial 19). Tutorial 13 has the textbook ranges; tutorial 14 the measurements.
5. **Memory controller.** After an L3 miss the request waits in a queue and is turned into DRAM commands: a read of an open row, a precharge and activate, or an activate on an idle bank. In the DDR4-3200 example of tutorial 16 these are 13.75, 27.5 and 41.25 ns.
6. **The fill returns** through the caches, the load is woken in the scheduler, its result is written back and it **retires in order** (tutorial 11).
7. **Meanwhile**, the prefetchers have seen the miss and may have started the next lines (tutorial 14); other independent loads overlapped with this one, up to the number of miss-status registers (memory-level parallelism).

| The data is in... | Time |
|---|---:|
| L1 | 0.4 to 1.3 ns |
| another core's cache | 56 to 102 ns |
| DRAM | 146 to 149 ns |
| DRAM, and the translation missed the TLB (in this VM) | 221 ns |
| a page that is not mapped yet (anonymous, first write) | 1.1 to 1.3 µs |
| a page that has to be read from the disk | 1.4 to 3.7 ms (4 MiB with read-ahead; section 14) |

**Where it goes wrong.** A random walk over a large array pays the TLB miss *and* the cache miss on every access (huge pages cut the first). A pointer chase cannot overlap its misses, an independent-index loop can (tutorial 14). Two threads touching different variables on one line pay the cache-to-cache price every time (false sharing, tutorial 19).

---

## 11. Walkthrough 4: power-on to `main`

This is the one event no earlier tutorial traced, and the only one where the x86 and Arm paths differ in structure. **Standard knowledge, not checked against a manual, except where I say the VM showed it.**

**On a PC or x86 server.**

1. **Reset.** Power is stable, the clocks run, and every core is reset. The first core starts in 16-bit real mode and fetches from the reset vector, `0xFFFFFFF0`, just under 4 GiB, which the chipset maps to the firmware flash chip. The other cores wait.
2. **Firmware (UEFI, or the older BIOS).** Early code runs from flash with no working memory at all (it uses the cache as RAM), applies microcode patches, and **trains the DRAM** (finding timings per channel; tens of seconds on a large server). The firmware then enumerates the PCIe tree and assigns the BARs (tutorial 25 section 6), builds the **ACPI tables** (the description of cores, interrupts, devices and power states that the kernel will read), builds the memory map (which ranges are RAM, which are reserved), and starts the other cores. With secure boot every stage checks the next one's signature (tutorial 30 section 8).
3. **The bootloader** (GRUB or a similar program, or the kernel itself as a UEFI application) is found on the EFI system partition, loads the kernel image and the initial RAM disk into memory, and jumps to the kernel with the memory map and the command line.
4. **The kernel's early code** runs on one core: it switches to 64-bit mode and its own page tables if the firmware has not, decompresses itself, and calls `start_kernel`. That sets up the memory allocator, the interrupt controller and timers, brings the other cores up, probes the devices the firmware described, and mounts the root file system from the initial RAM disk.
5. **PID 1.** The kernel runs `/init` or `/sbin/init` in user space (an `execve`, walkthrough 1), which starts everything else, down to the shell, which runs your program, which reaches `main`.

**On an Arm server or SoC.** The boot ROM (burned into the chip, unmodifiable, holding the root-of-trust key) loads the first-stage firmware; in the Arm Trusted Firmware flow the stages are BL1 (boot ROM), BL2 (loads the rest), **BL31** (stays resident at EL3 and answers `SMC` calls such as the standard CPU on/off interface), optionally BL32 (a trusted OS at secure EL1) and BL33 (UEFI or U-Boot, run at EL2 or EL1). The kernel gets a **device tree** or ACPI tables describing the hardware. There is no single universal reset address as on x86; each SoC defines its own.

**What this VM showed.** The kernel log (`dmesg`) is a timeline from the kernel's first line:

```text
[    0.000000] Linux version 6.18.44-fc-v153 ...
[    0.000000] BIOS-e820: [mem 0x0000000000000000-0x000000000009fbff] usable
[    0.000000] BIOS-e820: [mem 0x0000000000100000-0x00000000bfffffff] usable
[    0.000000] BIOS-e820: [mem 0x00000000eec00000-0x00000000febfffff] reserved
[    0.000000] BIOS-e820: [mem 0x0000000100000000-0x000000023fffffff] usable
[    0.000000] Hypervisor detected: KVM
[    0.000074] tsc: Detected 2100.000 MHz processor
[    0.396280] x2apic enabled
[    0.411947] Spectre V2 : Mitigation: Enhanced / Automatic IBRS
[    0.424748] smp: Brought up 1 node, 2 CPUs
[    0.431958] Memory: 3215604K/8388216K available (19210K kernel code, ...)
[    0.465799] PCI: ECAM [mem 0xeec00000-0xeecfffff] ... for domain 0000 [bus 00-00]
[    0.717590] Unpacking initramfs...
[    0.788323] virtio-pci 0000:00:01.0: enabling device (0000 -> 0002)
[    0.839630] virtio_blk virtio1: [vda] 536870912 512-byte logical blocks (275 GB/256 GiB)
[    0.961876] Freeing unused kernel image (initmem) memory: 3940K
[    2.276141] random: crng reseeded due to virtual machine fork
```

- **There is no firmware stage in the guest.** The hypervisor placed the kernel in memory and started the CPU at its entry with the memory map and command line already in place; the log has no BIOS or UEFI phase (the "BIOS-e820" label is only the name of the format). `/sys/firmware/efi` does not exist (tutorial 30). Everything from "real mode to long mode" and "DRAM training" happened, if at all, on the host.
- **The memory map is the one the kernel was given.** RAM below 3 GiB, a hole below 4 GiB for device registers (the PCI configuration window at `0xeec00000` is inside it), and the remaining 5 GiB above 4 GiB, 8 GiB in all. The physical frames in section 15 all fall in the upper range, which is why they begin with `0x1`.
- **The kernel took 0.975 s to reach user space** (the line `Run ... as init process` is at 0.974773), of which unpacking the initial RAM disk (15,168 KB freed at 0.799 s) and probing ten virtio devices (0.788 s to about 0.87 s) are visible chunks. The first user-space program is the sandbox's own agent, not `systemd`. At 2.28 s the log says the random pool was reseeded "due to virtual machine fork" and the virtio disks report new sizes, which is what a clone from a snapshot looks like (my inference from those lines; I did not look further). So the 0.97 s is the boot of the original machine, and a PC's seconds-long firmware stage does not appear.
- **`deferred_init=lazy`** on the command line explains `3,215,604K` of `8,388,216K` available at boot: the line `1244624 pages left to deferred_grow_zone()` (4.75 GiB) shows the kernel did not initialise all page structures up front, and adds them as memory is needed.

**Where it goes wrong.** Boot problems are almost always in the earliest stage that has no console (DRAM training, the first firmware phases), and are debugged with a POST code or a serial line; the kernel's `earlyprintk` or `console=ttyS0` (as on this command line) is the first place the kernel can talk. A kernel that boots on a PC and not on an Arm board usually has a missing driver or a wrong device-tree entry.

---

## 12. Walkthrough 5: a system call

**Trigger.** A user program wants the kernel to do something it may not do itself. Tutorial 26 section 2.3 gives the instruction semantics; the point of this walkthrough is the whole round trip, so here are the two smallest possible programs, which make a `write` and an `exit` with no C library, one per ISA (I ran both, the Arm one under QEMU; each prints `hello`):

```text
x86-64 (sys_x86.S)                     AArch64 (sys_a64.S)
 mov  $1, %eax        # write           mov  x8, #64         // write
 mov  $1, %edi        # fd              mov  x0, #1          // fd
 lea  msg(%rip), %rsi # buffer          adr  x1, msg         // buffer
 mov  $6, %edx        # length          mov  x2, #6          // length
 syscall                                svc  #0
 mov  $60, %eax       # exit            mov  x8, #93         // exit
 xor  %edi, %edi                        mov  x0, #0
 syscall                                svc  #0
```

The numbers differ per architecture (`write` is 1 on x86-64 and 64 on AArch64; `exit` is 60 and 93), and so does the argument register for the number (`rax` and `x8`). After the `execve` that started it, `strace ./sys_x86` showed exactly two calls, `write(1, "hello\n", 6)` and `exit(0)`.

**Path (x86-64).**

1. `syscall` saves the return address in `rcx` and the flags in `r11`, clears the interrupt flag, switches to ring 0 and jumps to the address in `IA32_LSTAR`, with no memory access and **no stack switch**.
2. The kernel's entry code runs `swapgs` to find its per-CPU data, switches to the kernel stack, and pushes all user registers into a `pt_regs` frame. With KPTI (tutorial 30 section 4.2) it also writes `CR3` here; this VM's CPU does not need it.
3. The common C entry does the bookkeeping that tracing, auditing and seccomp need, then **indexes the system call table with the number**, masked so that a wrong guess cannot be speculated past (the Spectre v1 fix of tutorial 30 section 4.3), and calls the handler.
4. The handler works. Before returning, the kernel checks the pending work flags: a signal to deliver, a reschedule request, a restartable-sequence fixup (walkthrough 6 happens here if the thread must give up the CPU).
5. `sysretq` restores `rip`, `rflags` and the user stack and drops to ring 3.

**On AArch64:** `svc` raises a synchronous exception to EL1; the CPU saves the return address in `ELR_EL1` and the state in `SPSR_EL1`, records the cause in `ESR_EL1` (an exception class that says "SVC from AArch64", standard), and vectors to the "lower EL, AArch64, synchronous" entry at offset `0x400` (tutorial 26 section 2.4). The kernel saves registers, dispatches through its table indexed by `x8`, and `eret` returns. The sequence is the same; only the mechanism for entry and return differs.

**Cost.** 115 to 126 ns for a system call that does nothing, about 262 TSC ticks (tutorial 26). That is the *crossing*; the work behind the call is extra. The vDSO avoids the crossing for `clock_gettime` (31 ns against about 190 ns). Ten million calls per second cost over one second of CPU time per second, which is why batching interfaces exist (`readv`, `sendmmsg`, `io_uring`). With KPTI the cost is higher by a `CR3` write on entry and another on exit (about a hundred cycles each, per the kernel documentation, tutorial 30; not active on this CPU).

**Where it goes wrong.** A system call per tiny operation in a hot path; `strace` itself slows the traced program by an order of magnitude because it stops the process at every call. A blocking call is where a context switch hides (next walkthrough).

---

## 13. Walkthrough 6: a context switch

**Trigger.** The running thread blocks (a `read` on an empty pipe), is preempted (its time slice ends, a timer interrupt), or a more urgent thread wakes. In every case the kernel is already running on the thread's behalf, with its user registers saved in `pt_regs` by the entry code of walkthrough 5.

**Path.**

1. **`schedule()`** picks the next runnable thread for this CPU (the scheduler's data is per CPU; tutorial 18).
2. **Address space.** If the next thread belongs to a different process, the kernel writes the new page-table root (`CR3` with a PCID, or `TTBR0` with an ASID on Arm), so the TLB entries of the old process stay tagged and stay in the TLB (tutorial 15 section 10.1). If it is another thread of the same process, nothing changes. If it is a kernel thread, the kernel simply keeps the old address space.
3. **The register switch.** The kernel saves the callee-saved registers and the kernel stack pointer of the outgoing thread, loads those of the incoming one, and updates the per-CPU `current` pointer, the thread-local base (`FS` base) and, if needed, debug registers. This is a few dozen instructions.
4. **Vector and floating-point state.** The outgoing thread's `XSAVE` area is written to memory and the incoming thread's is restored before it returns to user mode (section 5.3: up to 11,008 bytes here). With an `XSAVEOPT`/`XSAVEC`-style instruction, components in their initial state cost nothing (standard). On Arm the SVE state is `34 × VL` bytes (section 5.3) and is saved when needed.
5. **Return to user mode** (walkthrough 5, steps 4 and 5): the thread that wakes up sees its own `read` return.
6. **After the switch**, the new thread runs on **caches, a TLB and branch predictors filled by someone else**. That indirect cost is usually larger than the switch itself (tutorial 15 section 10.2).

**Cost, measured.** Two tasks pinned to one CPU pass a byte back and forth through two pipes (a round trip is a write, a block, a wake, a switch, a read, and the same again). Each cell is the range over three runs of 200,000 round trips, one-way (half a round trip), in nanoseconds:

| Tasks | no vector state live | 4 `ymm` registers live | 8 `zmm` registers live (including `zmm16`, `zmm17`, `zmm30`, `zmm31`) |
|---|---:|---:|---:|
| two processes | 1,223 to 1,477 | 1,259 to 1,541 | 1,290 to 1,375 |
| two threads of one process | 1,238 to 1,450 | 1,221 to 1,352 | 1,224 to 1,379 |

Across all 18 runs the one-way hand-off was **1.2 to 1.5 µs**, and the spread between runs is larger than any difference between the columns or between the rows. So in this VM I **cannot see** a cost for switching address spaces (the PCID keeps the TLB, and this test touches almost no memory) nor for the extra vector state (the kernel saves what is live, but the effect is below the noise). The futex hand-off of tutorial 26 (a different primitive) was 1.6 to 1.8 µs. It does not say a switch is cheap in a real program: the test has no working set to lose. To see the indirect cost, give both tasks a working set that fills the L2 cache and compare their run time with and without switching between them; `perf bench sched pipe` (add `-T` for threads) is the ready-made version of this pipe test on a machine that has `perf`.

**Where it goes wrong.** Too many runnable threads per core: the switches and the cold caches add up. A thread that bounces between CPUs loses its cache contents; pinning, or an isolated core, avoids it. A wake-up on another CPU needs an inter-processor interrupt (10 µs in this VM); a spinning waiter avoids it (60 ns), at the price of a busy core.

---

## 14. Walkthrough 7: a page fault

**Trigger.** A load, store or instruction fetch finds a not-present entry or a permission mismatch during translation (walkthrough 3, step 3). Tutorial 15 section 7 covers the mechanism; this walkthrough adds the full cost table.

**Path (x86-64).**

1. The core raises `#PF` (vector 14) and writes the faulting address to `CR2` and the cause (not present or protection, read or write, user or kernel, instruction fetch) into the error code. The faulting instruction has not completed, so it can be re-executed (a *precise* exception).
2. Entry as in walkthrough 5 (an IDT gate instead of `LSTAR`): registers saved, kernel stack.
3. The kernel looks up the **region** (the VMA) that covers the address. None: the program touched an unmapped address, and it gets `SIGSEGV` with `SEGV_MAPERR`. A region that forbids this kind of access: `SIGSEGV` with `SEGV_ACCERR`. Otherwise the fault is legitimate.
4. By region type: **anonymous, first read**: map the shared zero page (read-only). **Anonymous, write**: allocate a frame, zero it, map it. **File-backed**: find the page in the page cache and map it (the kernel maps several neighbouring pages at once, "fault-around"); if it is not cached, start the read and sleep (a **major** fault). **Write to a private page that is shared**: copy it (copy-on-write). **Swapped out**: read it back.
5. Write the page-table entry. Return from the exception; the CPU re-executes the instruction, which now translates.

**On AArch64:** a synchronous data abort or instruction abort to EL1, with the cause in `ESR_EL1` and the address in `FAR_EL1` instead of `CR2` (tutorial 26). The kernel logic is the same code.

**Cost, measured.** Touching one byte in every page of a 4 MiB mapping (1,024 pages), counting faults with `getrusage` and timing per page (three runs, transparent huge pages off for the anonymous case):

| What | Faults (minor / major) | Time per page |
|---|---:|---:|
| anonymous, first **write** to each page | 1,027 / 0 | 1.14 to 1.32 µs |
| the same pages, second write | 0 / 0 | 7 to 11 ns |
| anonymous, first **read** of each page | 1,024 / 0 | 0.61 to 0.71 µs |
| ...then the first **write** to those pages | 1,024 / 0 | 1.91 to 1.97 µs |
| `mmap` with `MAP_POPULATE` | 1,024 / 0 (inside the `mmap` call) | 1.05 to 1.13 µs |
| ...then touching it | 0 / 0 | 8 to 12 ns |
| a file in the page cache, read each page | 2 / 0 | 10 to 17 ns |
| the file after `POSIX_FADV_DONTNEED`, read each page | 36 / 1 | 1.40 to 3.63 µs (1.4 to 3.7 ms in total) |

What the table teaches:

- **Reading before writing costs two faults per page**: the read maps the zero page (0.6 to 0.7 µs), the later write breaks it into a real page (1.9 to 2.0 µs), 2.6 µs in total against 1.2 µs for writing first. Code that scans a freshly allocated buffer before filling it pays double.
- **`MAP_POPULATE` is not much cheaper per page** in this session (1.05 to 1.13 µs against 1.14 to 1.32 µs): the zeroing and the page-table work remain. What it buys is **predictability**: afterwards the access is 8 to 12 ns, with no fault in the request path.
- **The cached file took 2 faults for 1,024 pages.** That is far fewer than one fault per page, so each fault must have mapped many pages that were already in the cache; large page-cache blocks written by one big `write` would explain it, but I did not confirm the reason. The point is that mapping a cached file is far cheaper than anonymous memory, which needs a zeroed frame for every page.
- **A major fault is three orders of magnitude more**: the one major fault started a read-ahead of the whole 4 MiB from the virtual disk, 1.4 to 3.7 ms in these runs. On a real disk with a cold cache it is longer. A data-plane thread must never take one (tutorial 15).

**Where it goes wrong.** First-request latency (memory touched for the first time on the request path: pre-fault it); a stop-the-world pause when a huge page is zeroed (0.3 ms); `SIGSEGV` from a bad pointer or a stack overflow into the guard page; a storm of faults when the working set exceeds memory.

---

## 15. Walkthrough 8: `malloc` down to DRAM

**Trigger.** `char *p = malloc(n); p[0] = 1;` Tutorial 15 asks the second half as an interview question; here is the whole chain, with a program that watches it (`mallocwalk`, 56 lines, in the Answers; the key function is quoted below).

**Path (glibc on Linux).**

1. **`malloc` looks in its own bins first.** A small request is served from a **thread cache** of recently freed blocks: no lock, no system call, 8 to 15 ns for a `malloc` and `free` pair (measured).
2. **Otherwise from the arena's top chunk**, which is a region of the heap. If the top chunk is too small, `malloc` asks the kernel to extend the heap with `brk`. The first `malloc(100)` in a fresh process called `brk` once and grew the heap by 135,168 bytes (132 KiB: the request plus a 128 KiB pad), so that the next hundreds of small allocations make no system call.
3. **A large request** (the default threshold is 128 KiB) is not taken from the heap but from a new anonymous mapping: `mmap(NULL, 1052672, ...)` for `malloc(1 << 20)` (the extra 4 KiB is the allocator's header page). `free` of such a block `munmap`s it. glibc then **raises the threshold** to the size it just freed, so the *next* 1 MiB `malloc` came from the heap (`brk`) instead; I watched it with `strace -e trace=brk,mmap,munmap`: `mmap`, `munmap`, then `brk`.
4. **At this point nothing is in RAM.** The kernel has only recorded a region of the address space. Before touching, the program's anonymous resident memory was 128 KB; after writing one byte to each of the block's 256 pages it was 1,148 KB (a few KB different from run to run). **Resident memory grows at the touch, not at the `malloc`.**
5. **The first touch faults** (walkthrough 7): the kernel takes a free frame from its page allocator, zeroes it, and writes the page-table entry.
6. **Which frame? Any.** I read `/proc/self/pagemap` (as root) for the first six pages of the touched block:

```text
page 0  0x11b4d9000     page 3  0x126263000
page 1  0x13a47d000     page 4  0x126923000
page 2  0x122312000     page 5  0x11bb39000
```

   The six consecutive virtual pages sit in six unrelated physical frames between 4.4 and 4.9 GiB (the upper RAM range of the memory map in section 11; the values differ on every run). **Virtually contiguous does not mean physically contiguous**; a device that needs contiguous physical memory needs a special allocator (tutorial 26 section 7). These are *guest-physical* addresses: the hypervisor translates them again (EPT, tutorial 27) to host frames the guest cannot see.
7. **The physical address reaches the memory system.** A load or store to it that misses in L3 goes to the memory controller, which splits the address into channel, rank, bank, row and column (tutorial 16) and issues the DRAM commands of walkthrough 3, step 5. The line is cached in L1, L2 and L3, and a later `free` leaves the data where it was.
8. **`free`** returns the block to a thread-cache bin (a few ns). The page stays resident, even though the program no longer uses it, until the allocator returns it to the kernel (`brk` trimmed, `munmap`, or `madvise`). This is why the RSS of a long-running service does not fall when it frees memory.

```c
// from mallocwalk.c: the physical frame behind a virtual address (needs root; otherwise the kernel hides the frame numbers)
static uint64_t frame(int pm, void *p) {            // pm = open("/proc/self/pagemap"); 0 if absent or hidden
    uint64_t e = 0; pread(pm, &e, 8, ((uintptr_t)p / 4096) * 8);       // one 64-bit entry per virtual page
    return (e >> 63) ? (e & ((1ULL << 55) - 1)) : 0;                   // bit 63: present; bits 0 to 54: frame number
}
```

**Cost, measured** (five runs):

| What | Time |
|---|---:|
| `malloc(64)` + write + `free` | 7.8 to 14.8 ns |
| `malloc(1 MiB)` + write + `free`, served from heap memory the allocator already holds | 19 to 36 ns |
| `mmap(1 MiB)` + write one page + `munmap` (what a large `malloc` and `free` cost when the kernel is involved) | 3.5 to 6.4 µs |
| `mmap(1 MiB)` + write all 256 pages + `munmap` | 250 to 300 µs (0.98 to 1.17 µs per page) |

A `malloc` and its first use span **three orders of magnitude** depending on the path: 10 ns (thread cache), 20 ns (heap reuse), 4 µs (kernel mapping), 1 µs per page for the touches. The common surprise is the second row: a freed-and-reallocated large block is cheap because the memory stayed resident, and a `free` that unmaps makes the *next* allocation pay the faults again.

**Where it goes wrong.** Latency spikes when a request is the first to touch a large buffer; RSS that stays high after `free` (fragmentation, a pinned top chunk, per-thread arenas: `malloc_trim` or a different allocator); a service that allocates and frees large blocks in a loop at the mmap threshold, paying 4 µs plus the faults every time; memory that is mapped but never touched, which is cheap and invisible in RSS.

**On Arm.** Identical logic. The page size (4, 16 or 64 KB) changes how many faults a buffer takes, and the 128-byte cache line of some Arm cores changes false-sharing padding.

---

## 16. What you see, and what it usually is

| What you see | Likely cause | First check |
|---|---|---|
| A short-lived tool is slow when started thousands of times | `fork`+`exec`, dynamic loading, cold page faults (about 1 ms here) | `strace -c`, `\time -f '%R faults'`; link statically, `posix_spawn` |
| The first request after a start or deploy is slow | memory and code faulting in on first use | pre-touch, `MAP_POPULATE`, `LD_BIND_NOW`, warm-up traffic |
| High `sys` time, few cycles per call | too many tiny system calls | `strace -c`; batch (`readv`, `sendmmsg`, `io_uring`) |
| 10 µs wake-up latency | a sleeping waiter woken across CPUs | spin, pin, or put both on one CPU |
| RSS did not grow after a big `malloc` | untouched memory | RSS grows at first touch |
| RSS did not shrink after `free` | memory held by the allocator | `malloc_trim`, `M_TRIM_THRESHOLD`, a different allocator |
| Many minor faults in a steady-state loop | memory returned to the kernel and re-requested | raise the mmap threshold, reuse buffers |
| A major fault in a data-plane thread | a page-cache miss or swap | `mlockall`, pre-read the file |
| Interrupts at tens of thousands per second on one core | one flow or no moderation | RSS and queues, interrupt moderation, NAPI budget |
| Works on x86, rare corruption or hang on Arm | memory ordering, alignment, `char` sign, fused multiply-add | sections 3 and 4; ThreadSanitizer |
| Different numerical results on Arm | `fmadd` contraction | `-ffp-contract=off` |
| JIT or self-modifying code crashes only on Arm | missing instruction-cache maintenance | `__builtin___clear_cache` |

---

## 17. Mistakes that cost points

1. **Answering "RISC against CISC".** Say encoding and memory ordering, then give a measured consequence of each. The evidence says the ISA label does not decide speed or energy.
2. **Calling x86 "denser".** On a whole program (53,000 lines of C, same compiler) AArch64 was 10 to 15 % smaller and used 14 to 15 % fewer instructions (x86 needed 17 % more at `-O2`). A single loop is not evidence.
3. **Trusting an x86 test of lock-free code.** `relaxed` and `release`/`acquire` are the same machine code there; the bug cannot fail. Use ThreadSanitizer and Arm hardware.
4. **Ignoring undefined behaviour that x86 happens to define.** Signed `char`, division by zero, float to integer, fused multiply-add and misaligned atomics all differ in section 4.
5. **Forgetting instruction-cache maintenance on Arm** for generated code.
6. **Using `QEMU` results as Arm performance data.** It runs the code correctly and says nothing about speed or ordering.
7. **Starting "what happens when" at the kernel.** Begin at the trigger and keep going down to the hardware; name the TLB and the cache without being asked.
8. **Giving no numbers.** "A page fault is slow" is not an answer; "about a microsecond and a bit for a minor fault, a thousand times that for a major one" is.
9. **Saying `fork` copies memory,** or that `malloc` allocates memory. `fork` copies page tables; `malloc` reserves addresses and hands out previously mapped memory; the page arrives at the first touch.
10. **Saying a context switch is "saving registers".** The registers are a microsecond; the cold caches and TLB afterwards are the cost.
11. **Forgetting the VM.** `cpuid` is 7.5 µs, a cross-CPU wake-up is 10 µs, a page walk is two-dimensional: say which numbers are from a guest.
12. **Believing a 4 KB page size.** It is the x86 constant; ask the kernel.


---

## 18. Questions you will be asked

**Q1. What actually differs between Arm and x86 for someone writing systems code?** Not "RISC against CISC". Five things you can name and measure. (1) **Memory ordering**: x86 is TSO (only a store followed by a load may be reordered), Arm is weakly ordered, so `release`/`acquire` compile to plain `mov` on x86 and to `stlr`/`ldar` on Arm; a missing ordering is invisible on x86. (2) **Registers and encoding**: 16 against 31 general registers, variable-length against fixed 4-byte instructions; in zstd at `-O2` AArch64 needed 15 % fewer instructions and 15 % fewer bytes, and only 10.5 % of its instructions were stack accesses against 19.9 % on x86. (3) **What the language leaves undefined**: `char` signedness, divide by zero, float to integer, fused multiply-add and misaligned atomics all behave differently. (4) **System architecture**: rings and MSRs against exception levels and system registers; reset vector and firmware against a trusted-firmware chain and a device tree; APIC against GIC; a 4 KB base page against a choice of 4, 16 or 64 KB. (5) **Vector extensions**: SSE/AVX at fixed widths against NEON and the length-agnostic SVE. Whether a particular chip is faster or more efficient is decided by its microarchitecture, not its ISA label (tutorial 12; Blem et al., HPCA 2013).

**Q2. Is x86 code denser than Arm code?** Not on a whole program. I compiled 53,692 lines of zstd with one compiler for both: `.text` was 567,576 bytes on x86-64 and 482,028 on AArch64 at `-O2` (0.85), 367,234 against 330,052 at `-Os` (0.90). The average x86 instruction was 4.01 bytes, the fixed AArch64 one 4.00, so the variable-length encoding bought nothing on average; x86 needed 17 % more instructions, mostly spills, two-operand copies and separate loads. Tutorial 06's one small loop went the other way, which is the point: do not generalise from one function.

**Q3. A lock-free queue passes every test on x86 and corrupts data on Arm. Why, and how do you find it without Arm hardware?** The producer writes the data and then publishes the index; on Arm the two stores may become visible in the other order unless the publish is a `release` store and the consumer's read of it an `acquire` load. On x86 the hardware keeps stores in order, so relaxed and release/acquire produce *identical machine code* and the bug cannot fail; QEMU on an x86 host did not show it either. Without Arm hardware: audit every publish/consume pair by hand, run ThreadSanitizer (it checks the C++ happens-before relation, so it flags the race on x86; Problem A), read the `ldar`/`stlr` in the Arm disassembly, and model-check the core primitive. Test on the weakest real core you ship. Do not "fix" it with a `dmb` after every store.

**Q4. What do you check when porting C or C++ from x86 to Arm?** In order of how often they bite: memory ordering and atomics (Q3); `char` is unsigned on Arm Linux, so `char c = 200; c < 0` flips; `long double` is 113 mantissa bits against 64; integer division by zero traps on x86 and returns 0 on AArch64, and `INT_MIN / -1` traps on x86 and does not on Arm; float to integer out of range gives `INT_MIN` on x86 and saturates on Arm, and NaN gives `INT_MIN` against 0; AArch64 compilers fuse `a*b+c` into `fmadd` at `-O2` by default (`-ffp-contract=off` restores identical results); atomics must be naturally aligned on some Arm cores (a `SIGBUS` on `cortex-a57` in my run); generated code needs explicit instruction-cache maintenance (`__builtin___clear_cache`); and the page size may not be 4 KB. Sections 3 and 4 ran or compiled everything except the page size on both ISAs.

**Q5. What happens when you run `./a.out` from a shell?** The shell `fork`s (page tables copied, private pages copy-on-write), the child calls `execve`, which drops the old address space, maps the segments of the file *without reading them*, builds the stack with `argv`, `envp` and the auxiliary vector, randomises the base addresses, and, for a dynamic binary, starts the loader named in `PT_INTERP`. Every page the program touches for the first time then faults in (file-backed from the page cache, or zeroed anonymous). The loader maps `libc`, sets the thread pointer, processes relocations, runs the constructors and calls `main`; at exit the kernel tears the address space down and the shell's `wait` returns. Cost here: about 1 ms for a trivial dynamic program (75 minor faults, 35 system calls), 0.8 to 0.9 ms linked statically (46 faults, 17 calls). The first things to name are `fork`, `execve`, the loader and demand paging.

**Q6. What happens, in hardware, when a program dereferences a pointer?** The address is generated in the load unit; the L1 TLB is searched in parallel with the L1 cache index; a TLB miss goes to the second-level TLB (about 7 cycles) and then to a page walk (4 dependent reads, up to 24 in a virtual machine, 45 to 75 ns extra here); a permission or presence failure is a page fault; the physical tag is compared with the L1 line (about 5 cycles on a hit); a miss goes to L2 (about 5 ns), L3 (about 55 ns) and DRAM (146 to 149 ns, 221 ns with a TLB miss), where the memory controller turns it into row commands (13.75, 27.5 or 41.25 ns in the DDR4-3200 example); the fill returns, the load retires in order. Independent loads overlap (about 12 in flight on this core), a pointer chase cannot.

**Q7. Walk me through power-on to `main`, on x86 and on Arm.** x86: reset, the first core fetches from `0xFFFFFFF0` (the firmware flash), firmware trains the DRAM, enumerates PCIe, builds the ACPI tables and memory map, and starts the other cores; a bootloader loads the kernel and an initial RAM disk; the kernel switches to its own page tables, initialises memory, interrupts and timers, probes devices, mounts the root file system and runs `init`, which eventually starts your program. Arm: an immutable boot ROM verifies and loads the first firmware stage (in the Arm Trusted Firmware flow BL1, BL2, BL31 which stays resident at EL3, optionally a trusted OS, then UEFI or U-Boot), and the kernel is given a device tree or ACPI. Each stage verifies the next in a secure-boot chain (tutorial 30 section 8). In a micro-VM, as here, none of the firmware stages exist in the guest: the hypervisor starts the kernel directly and `dmesg` shows 0.975 s from the kernel's first line to `init`. (The reset vector and the Arm stage names are standard knowledge I did not verify.)

**Q8. What is in the cost of a system call?** The crossing, not the work: `syscall` saves `rip` and `rflags` in `rcx` and `r11` and jumps to the kernel entry (no memory access, no stack switch), the kernel does `swapgs`, switches to its stack, saves the registers, indexes the table with the (masked) number, runs the handler, checks for pending work (signals, reschedule) and returns with `sysretq`. 115 to 126 ns for `getppid` here, about 262 TSC ticks; KPTI adds a `CR3` write each way (about a hundred cycles each, not active on this CPU); the vDSO avoids the crossing for `clock_gettime` (31 ns against about 190 ns). On AArch64 `svc` vectors to EL1 at offset `0x400`, with the same structure. Batch (`readv`, `sendmmsg`, `io_uring`) or avoid the call; do not `strace` a hot path.

**Q9. How expensive is a context switch?** The register switch is tens of instructions; the cost is what happens after: the address-space change (cheap with PCID or ASIDs, which keep the TLB), the saved vector state (up to 11,008 bytes of `XSAVE` area here), and above all the incoming thread running on caches, a TLB and predictors filled by someone else. A pipe hand-off between two tasks on one CPU took 1.2 to 1.5 µs one way, and I could not see any difference between two processes and two threads, or between no vector state and eight live `zmm` registers: the test has no working set to lose. A wake-up that crosses CPUs costs about 10 µs here against 60 ns for a spinning waiter.

**Q10. Does `malloc` allocate memory?** It reserves addresses and hands out memory it already holds. A small request comes from a thread cache (8 to 15 ns, no system call); when the heap is short glibc extends it with `brk` (132 KiB at first); a request of 128 KiB or more gets its own `mmap` (the threshold then rises to the size you freed, so the next one may come from the heap). Nothing is resident until the first touch: the anonymous resident size went from 128 KB to 1,148 KB only when the 256 pages were written, each a fault of about 1.1 to 1.3 µs and a frame zeroed by the kernel. The frames are scattered in physical memory. `free` usually does not return memory to the kernel, so the resident size stays high.

**Q11. The first request after a deploy is slow. Where do you look?** Everything that was lazy: pages of the binary and its libraries faulting in from the page cache (or the disk: a major fault is 1.4 to 3.7 ms for 4 MiB here), relocations and `ifunc` resolution, anonymous buffers faulting on first write (about 1.2 µs per 4 KB page, 1.2 to 1.4 ms per 4 MiB), reads that first map the shared zero page and then fault again on the write (2.6 µs per page instead of 1.2), connection and cache warm-up. Fixes: pre-touch or `MAP_POPULATE` (about the same cost per page, but paid before the first request), `mlockall` for the latency path, `LD_BIND_NOW`, warm-up traffic before the instance joins the load balancer.

**Q12. Can one core keep up with 10 Gb/s of minimum-size packets?** A 64-byte frame occupies 84 bytes on the wire, so 14.88 million packets a second, 67 ns each, about 200 cycles at 3 GHz. One system call is 115 to 126 ns, so one call per packet already fails; one interrupt per packet is out of the question (a VM exit alone is about 7 µs here). What works is batching: NAPI turns one interrupt into a poll that handles many packets, receive aggregation makes each packet larger (about 6 to 7 KB here), and kernel-bypass frameworks poll the ring from user space with no interrupt, no copy and no wake-up, at the price of a busy core. Problem D does the arithmetic.


---

## 19. Problems

**How to use this section.** Four problems. Each has paper parts, a short program or script in the Answers that checks them, and a *Where* and *How* line saying where to check and how to start. Do the paper part first, then run the check, and fix the paper answer if they disagree. Time: about 30 minutes each. Nothing here attacks anything. The Arm programs are only compiled with a cross compiler and run under QEMU's user-mode emulation, which checks results and not timing or memory ordering; I have no Arm timings and none are claimed.

### Problem A (paper, compiler, sanitizer): port a lock-free ring buffer from x86 to Arm

A single-producer, single-consumer ring buffer. The producer owns `tail`, the consumer owns `head`; the orderings are left blank:

```cpp
bool push(int v) {
    unsigned t = tail.load(???);                    // 1
    if (t - head.load(???) == N) return false;      // 2   full
    slot[t % N] = v;
    tail.store(t + 1, ???);                         // 3
    return true;
}
bool pop(int &v) {
    unsigned h = head.load(???);                    // 4
    if (tail.load(???) == h) return false;          // 5   empty
    v = slot[h % N];
    head.store(h + 1, ???);                         // 6
    return true;
}
```

(a) Choose the weakest correct memory order for each of the six accesses and name the pairs that need one another. Which pair is easy to forget, and what goes wrong without it?
(b) Predict what differs in the compiled `push` between the all-relaxed version and the correct one, on x86-64 and on AArch64. Then check with `objdump`.
(c) Predict the result of a test that sends 20 million integers through the ring and counts the ones that arrive wrong, for the all-relaxed version, on x86 and under QEMU. What does a pass prove?
(d) Predict what ThreadSanitizer says about each version, and on which line.
(e) The all-relaxed version is also wrong for a reason that has nothing to do with the CPU. What is it? Does `volatile` help?
(f) *On your machine:* build everything in the Answers. If you have real Arm hardware (a Raspberry Pi, an Arm cloud instance, an Apple-silicon Linux VM), run the all-relaxed build on it for a long time under load. Also benchmark a contended `fetch_add` built with `-march=armv8-a` (load-exclusive/store-exclusive) and with `-march=armv8.1-a` (single-instruction atomics) at 2 and 4 threads, and keep a file of every number on the Arm machine that disagrees with the x86 ones in this series: that file is worth more than any general claim.

*Where:* (a) to (e) on paper, checked by `spsc.cpp` and `objdump`; (f) any Linux machine with `g++`; the Arm part needs `aarch64-linux-gnu-g++` and `qemu-aarch64` (or a real board).
*How:* (a) For each load ask "which store's data does seeing this value let me read?" and "which read must finish before this store hands the slot back?". (b) Look for `ldar`/`stlr` against `ldr`/`str`. (c) Section 3's litmus test gave all zeros. (e) The C++ memory model constrains the *compiler* too.

### Problem B (paper, then both compilers): the traps, the density and the saved state

(a) Read `pitfalls.c` in the Answers and, before running anything, write down what each of its lines prints on AArch64 Linux. For each difference from x86, give the portable fix.
(b) Take a C project of your own, or any single-file C library, and compile it at `-O2` for x86-64 and for AArch64 with `-fno-stack-protector`, `-fcf-protection=none` (x86) and `-mbranch-protection=none` (Arm). Run `density.py` on the two object files. Report instructions, bytes, bytes per instruction and the fraction of instructions that touch the stack. Is the result the same sign as the zstd result in section 2? If not, why not?
(c) Run `xstate.c` on an x86 machine. From the component list, add up the offsets and sizes to get the `XSAVE` area. Which features are enabled in `XCR0`? What is the minimum signal stack, and why is it larger than the area itself?
(d) For Arm SVE, state is `32 × VL + 17 × VL / 8` bytes plus control registers, with `VL` the vector length in bytes. Compute it at vector lengths of 128, 256, 512 and 2048 bits and compare with the x86 figure from (c).

*Where:* (a) paper, then run; (b) any machine with GCC and the cross compiler; (c) any x86 Linux machine.
*How:* (a) Section 4 lists the six behaviours. (b) Use the `curl`/`tar`/`gcc` commands in the Answers for the exact experiment. (c) Leaf `0xd` of `cpuid` gives, for each component, its size and offset. (d) One line of arithmetic per vector length.

### Problem C (paper, then run): the cost ledger

Use only the ledger in section 7 and the tables in sections 8, 14 and 15.

(a) A program `malloc`s 1 GiB and `memset`s it. How many page faults, and how long do they take? Repeat for: reading each page before writing it; `MAP_POPULATE`; 2 MB huge pages.
(b) A service maps a fresh 4 MiB buffer for every request, fills it and unmaps it. What does that cost per request in faults alone, and what request rate does it limit one core to?
(c) A script runs a small program 10,000 times from a loop. Estimate its run time. Name two changes that remove most of it.
(d) A process frees a 1 GiB buffer and its resident size does not move. Give three reasons and the tool that distinguishes each.
(e) *On your machine:* run `faults.c`, `mallocwalk.c` and `spawn.c` from the Answers and fill in your own column of the ledger. Where does your machine differ from this VM by more than a factor of two, and why do you think so?

*Where:* (a) to (d) on paper, checked by `budget.py`; (e) any Linux machine (`pagemap` needs root).
*How:* (a) Pages are `1 GiB / 4 KiB`; multiply by the per-page time. (b) Multiply by 1,024 pages and divide one second by the result. (d) Think of the allocator, of where the freed block sits in the heap, and of whether it was ever resident.

### Problem D (paper): the packet budget

(a) How many 64-byte frames per second fit on 10, 25 and 100 Gb/s Ethernet (a frame occupies 64 bytes plus 8 for the preamble and 12 for the inter-frame gap on the wire)? What is the time per packet, and how many cycles at 3 GHz?
(b) Compare the 10 Gb/s budget with a raw system call, a one-way context switch, a VM exit and a cross-CPU wake-up, all measured in this tutorial series. Which of them can happen once per packet?
(c) Suppose an interrupt costs 5 µs in total (assumed) and a packet needs 30 ns of real work (assumed). How many packets must each interrupt cover for the 10 Gb/s budget to hold? The measured figure at low load in section 9 was 1.7. What mechanism raises it at high load?
(d) If the kernel stack costs 1 µs per packet (assumed), how many cores does 14.88 Mpps need? What if a polling user-space stack costs 50 ns per packet (assumed)? What else limits the first design in this VM (see the queue count of tutorial 26 section 8.6)?
(e) Say which inputs to (a) to (d) are measured and which are assumed, and which assumption you would measure first.

*Where:* paper, checked by `budget.py`.
*How:* (a) `rate = bits per second / (84 × 8)`. (c) Solve `event / B + work ≤ budget` for `B`. (d) `cores = packets per second × time per packet`.

---

## Answers

### Problem A

**(a)** The orderings and the pairs:

| # | Access | Order | Why |
|---|---|---|---|
| 1 | `tail.load` in `push` | relaxed | only the producer writes `tail`; it reads its own value |
| 2 | `head.load` in `push` | **acquire** | pairs with 6: the consumer has finished *reading* the slot before the producer overwrites it |
| 3 | `tail.store` in `push` | **release** | pairs with 5: everything written before it, including `slot[...] = v`, is visible to a reader that sees the new `tail` |
| 4 | `head.load` in `pop` | relaxed | only the consumer writes `head` |
| 5 | `tail.load` in `pop` | **acquire** | pairs with 3: seeing the new `tail` means seeing the data |
| 6 | `head.store` in `pop` | **release** | pairs with 2: the read of the slot completes before the slot is handed back |

Pair 3 and 5 is the obvious one (data goes producer to consumer). Pair 6 and 2 is the one people forget, because the data does not flow that way, but the slot is *reused*: without it the producer on a weak machine may overwrite a slot the consumer is still reading, and the consumer gets a value from the wrong round.

**(b)** `spsc.cpp` below compiles `push` and `pop` out of line (`ring_push`, `ring_pop`) so that `objdump` shows exactly what they become. On x86-64 the two builds are **byte for byte identical**, and so is `ring_pop`; both use plain `mov` for every access. On AArch64 exactly two instructions differ:

```text
== x86-64, both builds (ring_push; ring_pop is also byte-identical between the two builds)
-- spsc_x86: md5 of the ring_push disassembly:
5ac2fc6bf1bb8616d0b067a8c5a4d3c0  -
-- spsc_x86_b: md5 of the ring_push disassembly:
5ac2fc6bf1bb8616d0b067a8c5a4d3c0  -
-- x86-64 ring_push (either build)
endbr64
mov    0x1004(%rdi),%eax
mov    0x1000(%rdi),%edx
mov    %eax,%ecx
sub    %edx,%ecx
xor    %edx,%edx
cmp    $0x400,%ecx
je     187 <ring_push+0x37>
mov    %eax,%edx
add    $0x1,%eax
and    $0x3ff,%edx
mov    %esi,(%rdi,%rdx,4)
mov    $0x1,%edx
mov    %eax,0x1004(%rdi)
mov    %edx,%eax
ret

== AArch64, release/acquire build: ring_push
mov	x2, #0x1004                	// #4100
add	x4, x0, x2
ldr	w2, [x4]
add	x3, x0, #0x1, lsl #12
ldar	w3, [x3]
sub	w3, w2, w3
cmp	w3, #0x400
b.eq	1b8 <ring_push+0x38>  // b.none
and	x3, x2, #0x3ff
add	w2, w2, #0x1
str	w1, [x0, x3, lsl #2]
stlr	w2, [x4]
mov	w0, #0x1                   	// #1
ret
mov	w0, #0x0                   	// #0
ret

== AArch64, -DBROKEN build: ring_push
mov	x2, #0x1004                	// #4100
add	x4, x0, x2
ldr	w2, [x4]
add	x3, x0, #0x1, lsl #12
ldr	w3, [x3]
sub	w3, w2, w3
cmp	w3, #0x400
b.eq	1b8 <ring_push+0x38>  // b.none
and	x3, x2, #0x3ff
add	w2, w2, #0x1
str	w1, [x0, x3, lsl #2]
str	w2, [x4]
mov	w0, #0x1                   	// #1
ret
mov	w0, #0x0                   	// #0
ret

== diff of the two AArch64 listings
5c5
< ldar	w3, [x3]
---
> ldr	w3, [x3]
12c12
< stlr	w2, [x4]
---
> str	w2, [x4]
```

The release/acquire build uses `ldar` for the load in 2 and `stlr` for the store in 3 (and the same two in `pop`); the all-relaxed build uses `ldr` and `str`, so nothing orders the store to the slot against the store to `tail`.

**(c)** Run on x86 and under QEMU:

```text
$ ./spsc            # correct, x86-64
20000000 values passed, 0 wrong
$ ./spsc_broken     # all relaxed, x86-64 (four runs, all the same)
20000000 values passed, 0 wrong
$ qemu-aarch64 ./spsc_a64 2000000           # correct, AArch64 under QEMU
2000000 values passed, 0 wrong
$ qemu-aarch64 ./spsc_a64_broken 2000000    # all relaxed, AArch64 under QEMU
2000000 values passed, 0 wrong
```

A pass proves nothing about ordering: x86 does not reorder these stores in hardware, the machine code of the two x86 builds is identical, and QEMU on an x86 host does not reorder the guest's accesses (standard). I cannot show you the failure here; that is the lesson of section 3. On real weakly ordered hardware the all-relaxed build is *allowed* to hand the consumer a stale slot, and whether it does depends on the core, the load and luck.

**(d)** ThreadSanitizer checks the C++ happens-before relation, not the hardware, so it sees the bug on x86. The correct build prints only its result line. The all-relaxed build prints:

```text
(excerpt)
WARNING: ThreadSanitizer: data race
  Read of size 4 at 0x562148e0b040 by thread T2:
    #0 Ring::pop(int&) spsc.cpp:31
  Previous write of size 4 at 0x562148e0b040 by thread T1:
    #0 Ring::push(int) spsc.cpp:24
  Location is global 'main::r' of size 4104
SUMMARY: ThreadSanitizer: data race spsc.cpp:31 in Ring::pop(int&)
```

Line 24 is `slot[t % N] = v` and line 31 is `v = slot[h % N]`: the plain data access has no happens-before edge to the other thread's access.

**(e)** With every ordering relaxed, the language allows the **compiler** to move the plain store `slot[...] = v` after the relaxed store to `tail`, or to hoist the consumer's read above its relaxed load. GCC 13 did not do it here (the x86 listing shows the slot store first), so x86 passes; a different compiler or version may. `volatile` does not help: it orders volatile accesses among themselves, not against ordinary data, and it emits no fence. The fix is the same: `release` and `acquire`.

**(f)** The program, with the build lines:

```cpp
// spsc.cpp: a single-producer, single-consumer ring buffer, and a test that checks every value arrives intact and in order.
//   correct:  g++ -O2 -std=c++17 -pthread spsc.cpp -o spsc          ./spsc [count]
//   the port that is wrong on Arm (all orderings relaxed):  g++ -O2 -std=c++17 -pthread -DBROKEN spsc.cpp -o spsc_broken
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

#ifdef BROKEN
  #define ACQ std::memory_order_relaxed
  #define REL std::memory_order_relaxed
#else
  #define ACQ std::memory_order_acquire
  #define REL std::memory_order_release
#endif

struct Ring {
    static constexpr unsigned N = 1024;
    int slot[N] = {};
    std::atomic<unsigned> head{0}, tail{0};          // the consumer advances head, the producer advances tail
    bool push(int v) {
        unsigned t = tail.load(std::memory_order_relaxed);          // only the producer writes tail
        if (t - head.load(ACQ) == N) return false;                  // full
        slot[t % N] = v;                                            // write the data ...
        tail.store(t + 1, REL);                                     // ... then publish it
        return true;
    }
    bool pop(int &v) {
        unsigned h = head.load(std::memory_order_relaxed);          // only the consumer writes head
        if (tail.load(ACQ) == h) return false;                      // empty
        v = slot[h % N];                                            // read the data ...
        head.store(h + 1, REL);                                     // ... then hand the slot back
        return true;
    }
};

// out-of-line copies, so that `objdump -d` shows exactly what push and pop compile to
extern "C" __attribute__((noinline)) bool ring_push(Ring &r, int v) { return r.push(v); }
extern "C" __attribute__((noinline)) bool ring_pop(Ring &r, int &v) { return r.pop(v); }

int main(int argc, char **argv) {
    const int COUNT = argc > 1 ? std::atoi(argv[1]) : 20'000'000;      // values to send
    static Ring r;
    long bad = 0;
    std::thread producer([&] { for (int i = 1; i <= COUNT; ) if (r.push(i)) i++; });
    std::thread consumer([&] { for (int i = 1; i <= COUNT; ) { int v; if (r.pop(v)) { if (v != i) bad++; i++; } } });
    producer.join(); consumer.join();
    std::printf("%d values passed, %ld wrong\n", COUNT, bad);
    return bad != 0;
}
```

```text
g++ -O2 -std=c++17 -pthread spsc.cpp -o spsc                      # correct
g++ -O2 -std=c++17 -pthread -DBROKEN spsc.cpp -o spsc_broken      # all relaxed
g++ -O1 -g -std=c++17 -pthread -fsanitize=thread spsc.cpp -o spsc_tsan                 # then: ./spsc_tsan 1000000
g++ -O1 -g -std=c++17 -pthread -fsanitize=thread -DBROKEN spsc.cpp -o spsc_broken_tsan # then: ./spsc_broken_tsan 1000000
aarch64-linux-gnu-g++ -O2 -std=c++17 -pthread -static spsc.cpp -o spsc_a64             # run: qemu-aarch64 ./spsc_a64 2000000
g++ -O2 -std=c++17 -c spsc.cpp -o spsc_x86.o && objdump -d --no-show-raw-insn spsc_x86.o        # look at ring_push
aarch64-linux-gnu-g++ -O2 -std=c++17 -c spsc.cpp -o spsc_a64.o && aarch64-linux-gnu-objdump -d --no-show-raw-insn spsc_a64.o
```

The message-passing litmus test of section 3 (`mp.cpp`) and the listing program behind its code-generation table (`cg.cpp`):

```cpp
// mp.cpp: the message-passing test.  Thread A: data = 1; flag = 1.   Thread B: r1 = flag; r2 = data.
// A violation is r1 == 1 with r2 == 0.  x86-TSO never allows it; Arm hardware may, unless release/acquire is used.
//   g++ -O2 -pthread -o mp_relaxed mp.cpp          g++ -O2 -pthread -DRELACQ -o mp_relacq mp.cpp
#include <atomic>
#include <cstdio>
#include <thread>
using std::memory_order_relaxed; using std::memory_order_release; using std::memory_order_acquire;
#ifdef RELACQ
constexpr auto ST = memory_order_release, LD = memory_order_acquire; const char *NAME = "release/acquire";
#else
constexpr auto ST = memory_order_relaxed, LD = memory_order_relaxed; const char *NAME = "relaxed";
#endif
constexpr int N = 2000000;
static std::atomic<int> data[N], flag[N];
int main() {
    long bad = 0;
    std::thread a([] { for (int i = 0; i < N; i++) { data[i].store(1, memory_order_relaxed);
                                                      std::atomic_signal_fence(std::memory_order_seq_cst);   // compiler-only fence: keeps the order in the code
                                                      flag[i].store(1, ST); } });
    std::thread b([&] { for (int i = 0; i < N; i++) { while (!flag[i].load(LD)) {}              // wait until round i's flag is set
                                                      std::atomic_signal_fence(std::memory_order_seq_cst);
                                                      if (!data[i].load(memory_order_relaxed)) bad++; } });   // then the data must be there
    a.join(); b.join();
    std::printf("%-16s: %ld of %d rounds saw the flag set but the data still 0\n", NAME, bad, N);
}
```

```cpp
#include <atomic>
int data; std::atomic<int> flag, counter;
extern "C" void send_relaxed()  { data = 42; flag.store(1, std::memory_order_relaxed); }
extern "C" void send_release()  { data = 42; flag.store(1, std::memory_order_release); }
extern "C" int  recv_relaxed()  { while (!flag.load(std::memory_order_relaxed)) {} return data; }
extern "C" int  recv_acquire()  { while (!flag.load(std::memory_order_acquire)) {} return data; }
extern "C" int  add_relaxed()   { return counter.fetch_add(1, std::memory_order_relaxed); }
extern "C" int  add_seqcst()    { return counter.fetch_add(1, std::memory_order_seq_cst); }
extern "C" void sync_code(char *a, char *b) { __builtin___clear_cache(a, b); }
```

```text
g++ -O2 -pthread -o mp_relaxed mp.cpp          g++ -O2 -pthread -DRELACQ -o mp_relacq mp.cpp          # x86, all zeros here
g++ -O2 -c cg.cpp -o cg_x86.o && objdump -d --no-show-raw-insn cg_x86.o
aarch64-linux-gnu-g++ -O2 -c cg.cpp -o cg_a64.o && aarch64-linux-gnu-objdump -d --no-show-raw-insn cg_a64.o
aarch64-linux-gnu-g++ -O2 -march=armv8.1-a -c cg.cpp ...      # the single-instruction atomics (ldadd, ldaddal)
aarch64-linux-gnu-g++ -O2 -mno-outline-atomics -c cg.cpp ...  # the ldxr/stxr loop in place of the library call
```

On real Arm hardware, expect `mp_relaxed` to be able to print a nonzero count and `mp_relacq` never to; I have not observed this and give no number.

### Problem B

**(a)** The program and what it printed (x86 natively; AArch64 under `qemu-aarch64 -cpu max`; the atomic line again under `-cpu cortex-a57`):

```c
// pitfalls.c: things that differ between x86-64 and AArch64 Linux, in code that "works".
//   gcc -O2 -fcf-protection=none -o pit_x86 pitfalls.c
//   aarch64-linux-gnu-gcc -O2 -o pit_a64 pitfalls.c && qemu-aarch64 -L /usr/aarch64-linux-gnu ./pit_a64
#include <stdio.h>
#include <stdint.h>
#include <float.h>
#include <limits.h>
#include <signal.h>
#include <setjmp.h>
#include <stdatomic.h>

static sigjmp_buf jb;
static void on_sig(int s) { siglongjmp(jb, s); }

static void try_int_div(const char *what, volatile int a, volatile int b) {
    if (sigsetjmp(jb, 1) == 0) printf("  %-22s -> %d\n", what, a / b);
    else printf("  %-22s -> signal %d (SIGFPE)\n", what, SIGFPE);
}

int main(void) {
    signal(SIGFPE, on_sig); signal(SIGBUS, on_sig);
    volatile char c = 200;                                  // plain char: signed on x86-64, unsigned on AArch64 Linux
    printf("char c = 200; c < 0 is %d  (CHAR_MIN = %d)\n", c < 0, CHAR_MIN);
    printf("long double: %zu bytes, %d mantissa bits\n", sizeof(long double), LDBL_MANT_DIG);
    printf("integer division:\n");
    try_int_div("7 / 0", 7, 0);
    try_int_div("INT_MIN / -1", INT_MIN, -1);
    volatile double nan = __builtin_nan(""), big = 1e10, neg = -1.0;
    printf("float to integer (undefined in C; this is what the hardware does):\n");
    printf("  (int)NaN             -> %d\n", (int)nan);
    printf("  (int)1e10            -> %d\n", (int)big);
    printf("  (unsigned)-1.0       -> %u\n", (unsigned)neg);
    volatile double a = 1.0 + 0x1p-30, b = 1.0 + 0x1p-29;   // a*a = 1 + 2^-29 + 2^-60
    double r = a * a - b;                                    // 0 if the product is rounded first, 2^-60 if fused
    printf("a*a - b with a = 1+2^-30, b = 1+2^-29 -> %g  (%s)\n", r, r == 0 ? "product rounded, then subtracted" : "fused multiply-subtract");
    printf("atomic increment at an address that is 2 mod 4:\n");
    static char buf[16] __attribute__((aligned(16)));
    if (sigsetjmp(jb, 1) == 0) { atomic_fetch_add((_Atomic int *)(buf + 2), 1); printf("  worked\n"); }
    else printf("  signal %d (SIGBUS)\n", SIGBUS);
    return 0;
}
```

```text
== x86-64 (native)
char c = 200; c < 0 is 1  (CHAR_MIN = -128)
long double: 16 bytes, 64 mantissa bits
integer division:
  7 / 0                  -> signal 8 (SIGFPE)
  INT_MIN / -1           -> signal 8 (SIGFPE)
float to integer (undefined in C; this is what the hardware does):
  (int)NaN             -> -2147483648
  (int)1e10            -> -2147483648
  (unsigned)-1.0       -> 4294967295
a*a - b with a = 1+2^-30, b = 1+2^-29 -> 0  (product rounded, then subtracted)
atomic increment at an address that is 2 mod 4:
  worked
== AArch64 (qemu-aarch64 -cpu max)
char c = 200; c < 0 is 0  (CHAR_MIN = 0)
long double: 16 bytes, 113 mantissa bits
integer division:
  7 / 0                  -> 0
  INT_MIN / -1           -> -2147483648
float to integer (undefined in C; this is what the hardware does):
  (int)NaN             -> 0
  (int)1e10            -> 2147483647
  (unsigned)-1.0       -> 0
a*a - b with a = 1+2^-30, b = 1+2^-29 -> 8.67362e-19  (fused multiply-subtract)
atomic increment at an address that is 2 mod 4:
  worked
== AArch64 (qemu-aarch64 -cpu cortex-a57), atomic line only
atomic increment at an address that is 2 mod 4:
  signal 7 (SIGBUS)
```

Build lines: `gcc -O2 -fcf-protection=none -o pit_x86 pitfalls.c`; `aarch64-linux-gnu-gcc -O2 -o pit_a64 pitfalls.c && qemu-aarch64 -L /usr/aarch64-linux-gnu ./pit_a64`; add `-cpu cortex-a57` to the QEMU line for the atomic test.

| Behaviour | x86-64 | AArch64 | Portable fix |
|---|---|---|---|
| plain `char` | signed | unsigned | write `signed char`, `unsigned char` or `int8_t`/`uint8_t`; or build with `-fsigned-char` as a stop-gap |
| `long double` | 64-bit mantissa (x87) | 113-bit mantissa (software quad) | do not depend on it; use `double`, or an explicit format on the wire |
| `7 / 0` | `SIGFPE` | 0 | test the divisor first |
| `INT_MIN / -1` | `SIGFPE` | `INT_MIN` | test for that pair first |
| `(int)NaN`, `(int)1e10`, `(unsigned)-1.0` | `INT_MIN`, `INT_MIN`, 4294967295 | 0, `INT_MAX`, 0 | check the range and NaN, then convert |
| `a*a - b` | rounds the product first (0) | fused multiply-subtract (about 8.7e-19) | `-ffp-contract=off`, or call `fma()` where you want it |
| atomic at a 2-mod-4 address | works | works on `-cpu max`, `SIGBUS` on `cortex-a57` | keep atomics naturally aligned; copy unaligned data with `memcpy` |

**(b)** The experiment, and what it printed. The source is the `zstd/zstd.c` amalgamation inside the `zstandard-0.25.0` source package on PyPI; any single-file C library will do.

```text
curl -sL -O https://files.pythonhosted.org/packages/source/z/zstandard/zstandard-0.25.0.tar.gz
tar xzf zstandard-0.25.0.tar.gz && cd zstandard-0.25.0
for O in -O2 -Os; do
  gcc $O -c -fno-stack-protector -fcf-protection=none -fno-pic -w -I zstd -o x86$O.o zstd/zstd.c
  aarch64-linux-gnu-gcc $O -c -fno-stack-protector -mbranch-protection=none -fno-pic -w -I zstd -o a64$O.o zstd/zstd.c
  echo "== $O"; python3 density.py x86$O.o a64$O.o
done
```

```text
== -O2
x86-64   141447 instructions  567576 bytes 4.01 B/instr | padding 5322 nops 29335 B | memory 48940 (34.6 %) stack 28159 (19.9 %)
  x86 length mix: 1B 1.9%, 2B 13.5%, 3B 30.0%, 4B 19.9%, 5B 19.4%, 6B 7.6%, 7B 3.5%, 8B 3.1%
  top: mov 34.0%, cmp 8.9%, add 6.9%, lea 5.5%, sub 4.5%, jmp 3.9%, je 2.7%, xor 2.4%
AArch64  120507 instructions  482028 bytes 4.00 B/instr | padding 1031 nops 4124 B | memory 32614 (27.1 %) stack 12658 (10.5 %)
  top: mov 12.9%, add 12.8%, ldr 11.6%, cmp 10.1%, sub 5.9%, str 5.7%, b 4.1%, ldp 3.1%
== -Os
x86-64    96342 instructions  367234 bytes 3.81 B/instr | padding 106 nops 836 B | memory 36639 (38.0 %) stack 21950 (22.8 %)
  x86 length mix: 1B 2.5%, 2B 19.4%, 3B 27.4%, 4B 19.9%, 5B 19.0%, 6B 4.7%, 7B 3.8%, 8B 2.5%
  top: mov 37.3%, cmp 7.1%, lea 5.7%, add 5.0%, sub 4.2%, jmp 3.6%, call 3.1%, xor 2.6%
AArch64   82513 instructions  330052 bytes 4.00 B/instr | padding 0 nops 0 B | memory 24041 (29.1 %) stack 9718 (11.8 %)
  top: mov 15.0%, ldr 12.9%, add 12.6%, cmp 7.8%, str 7.5%, sub 5.5%, b 4.1%, bl 3.4%
```

```python
# density.py: compare the .text of the same C file compiled for x86-64 and for AArch64.
#   python3 density.py x86.o a64.o
import subprocess, sys, re, collections

def is_padding(op, args):
    return op.startswith("nop") or op in ("data16", "cs", "ds") or (op == "xchg" and args.strip() == "%ax,%ax")

def scan(obj, objdump, x86):
    out = subprocess.run([objdump, "-d", "-w", "-j", ".text", obj], capture_output=True, text=True).stdout
    r = dict(n=0, bytes=0, pad=0, padb=0, mem=0, stack=0)
    lens, ops = collections.Counter(), collections.Counter()
    for line in out.splitlines():
        m = re.match(r"\s+[0-9a-f]+:\t((?:[0-9a-f]+ ?)+)\s*\t(\S+)\s*(.*)$", line)
        if not m: continue
        size = sum(len(t) // 2 for t in m.group(1).split())       # x86: 2 hex digits per byte; AArch64: one 8-digit word
        op, args = m.group(2), m.group(3)
        if is_padding(op, args): r["pad"] += 1; r["padb"] += size; continue
        r["n"] += 1; r["bytes"] += size; lens[size] += 1; ops[op] += 1
        if x86:
            if "(" in args and op != "lea": r["mem"] += 1
            if re.search(r"\(%[re][sb]p", args) and op != "lea": r["stack"] += 1
            if op in ("push", "pop"): r["mem"] += 1; r["stack"] += 1                # a push or pop is a stack access too
        else:
            if re.match(r"(ld|st)", op): r["mem"] += 1
            if re.match(r"(ld|st)", op) and re.search(r"\[(sp|x29)", args): r["stack"] += 1
    return r, lens, ops

for name, obj, tool, x86 in (("x86-64 ", sys.argv[1], "objdump", True), ("AArch64", sys.argv[2], "aarch64-linux-gnu-objdump", False)):
    r, lens, ops = scan(obj, tool, x86)
    n = r["n"]
    print(f"{name} {n:7d} instructions {r['bytes']:7d} bytes {r['bytes']/n:.2f} B/instr | padding {r['pad']} nops {r['padb']} B | "
          f"memory {r['mem']} ({100*r['mem']/n:.1f} %) stack {r['stack']} ({100*r['stack']/n:.1f} %)")
    if x86:
        print("  x86 length mix:", ", ".join(f"{k}B {100*v/n:.1f}%" for k, v in sorted(lens.items())[:8]))
    print("  top:", ", ".join(f"{k} {100*v/n:.1f}%" for k, v in ops.most_common(8)))
```

If your project comes out the other way (x86 smaller), check first that both builds use the same optimisation level and that neither carries extra protection code; then look at the instruction mix. A small loop with few live values favours x86's short encodings (tutorial 06's loop: 25 bytes against 48); a large program with many live values favours Arm's 31 registers and its fixed encoding.

**(c)** Run on this VM (`gcc -O2 -o xstate xstate.c`):

```text
vendor GenuineIntel, highest basic leaf 0x20
family 6 model 0xcf stepping 2, hypervisor bit 1
hypervisor leaf 0x40000000: "KVMKVMKVM"
XSAVE: features enabled in XCR0 = 0x600e7; area for them = 11008 bytes; largest possible = 11008 bytes
  component  2  AVX (ymm high halves)              256 bytes at offset 576
  component  5  AVX-512 opmask k0-k7                64 bytes at offset 1088
  component  6  AVX-512 zmm0-15 high 256 bits      512 bytes at offset 1152
  component  7  AVX-512 zmm16-31                  1024 bytes at offset 1664
  component 17  AMX tilecfg                         64 bytes at offset 2752 (64-byte aligned)
  component 18  AMX tiledata                      8192 bytes at offset 2816 (64-byte aligned)
minimum signal stack: sysconf(_SC_MINSIGSTKSZ) = 11952, _SC_SIGSTKSZ = 47808
```

```c
// xstate.c: what the CPU says about itself, and how big its saved state is.   gcc -O2 -o xstate xstate.c
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <cpuid.h>

int main(void) {
    unsigned a, b, c, d;
    __cpuid(0, a, b, c, d);
    char vendor[13]; memcpy(vendor, &b, 4); memcpy(vendor + 4, &d, 4); memcpy(vendor + 8, &c, 4); vendor[12] = 0;
    printf("vendor %s, highest basic leaf 0x%x\n", vendor, a);
    __cpuid(1, a, b, c, d);
    unsigned fam = (a >> 8) & 0xf, model = (a >> 4) & 0xf;
    if (fam == 6 || fam == 15) model |= ((a >> 16) & 0xf) << 4;
    if (fam == 15) fam += (a >> 20) & 0xff;
    printf("family %u model 0x%x stepping %u, hypervisor bit %u\n", fam, model, a & 0xf, (c >> 31) & 1);
    __cpuid(0x40000000, a, b, c, d);
    char hv[13]; memcpy(hv, &b, 4); memcpy(hv + 4, &c, 4); memcpy(hv + 8, &d, 4); hv[12] = 0;
    printf("hypervisor leaf 0x40000000: \"%s\"\n", hv);
    __cpuid_count(0xd, 0, a, b, c, d);
    printf("XSAVE: features enabled in XCR0 = 0x%x; area for them = %u bytes; largest possible = %u bytes\n", a, b, c);
    static const char *name[] = {"x87", "SSE", "AVX (ymm high halves)", "MPX bndregs", "MPX bndcsr", "AVX-512 opmask k0-k7",
                                 "AVX-512 zmm0-15 high 256 bits", "AVX-512 zmm16-31", "PT", "PKRU", "?", "?", "?", "?", "?", "?", "?", "AMX tilecfg", "AMX tiledata"};
    for (int i = 2; i < 19; i++) {
        __cpuid_count(0xd, i, a, b, c, d);
        if (a) printf("  component %2d  %-32s %5u bytes at offset %u%s\n", i, name[i], a, b, (c & 2) ? " (64-byte aligned)" : "");
    }
    printf("minimum signal stack: sysconf(_SC_MINSIGSTKSZ) = %ld, _SC_SIGSTKSZ = %ld\n", sysconf(_SC_MINSIGSTKSZ), sysconf(_SC_SIGSTKSZ));
    return 0;
}
```

`XCR0 = 0x600e7` has bits 0, 1, 2, 5, 6, 7 (x87, SSE, AVX, then the AVX-512 mask registers, the upper halves of `zmm0` to `zmm15` and `zmm16` to `zmm31`) and 17, 18 (the AMX tile configuration and tile data). The area is **not** the sum of the enabled parts: the first 576 bytes are the legacy region and header, and every component has a fixed offset, so there are gaps for features this CPU does not enable (256 bytes between the end of the AVX component at 832 and the mask registers at 1,088, and 64 bytes between 2,688 and the AMX configuration at 2,752; these are the slots of MPX and protection keys, standard and not checked). The size is the end of the last component: `2,816 + 8,192 = 11,008` bytes, as `cpuid` reports. A signal handler's frame holds that whole area plus the register context and signal information, which is why the minimum signal stack is 11,952 bytes (944 more) and the recommended one 47,808. The AMX tile data alone is 8 KB, about three quarters of the area.

**(d)** SVE state at `VL` bytes is `32 × VL + 17 × VL / 8`:

| Vector length | `VL` in bytes | State |
|---|---:|---:|
| 128 bits | 16 | 546 bytes |
| 256 bits | 32 | 1,092 bytes |
| 512 bits | 64 | 2,184 bytes |
| 2048 bits | 256 | 8,736 bytes |

Even the largest SVE length is smaller than this machine's AVX-512 plus AMX area, and a process can ask for a smaller length to shrink its state (the kernel documentation describes the `prctl`). The cost on x86 comes from enabling many features at once, on Arm from the vector length the hardware offers.

### Problem C

**(a)** `budget.py` (below) prints:

```text
== Problem C: first touch of a 1 GiB buffer = 262,144 pages of 4 KiB
  one write per page (1.14 to 1.32 us)                          : 0.30 to 0.35 s
  read each page first, then write (0.61+1.91 to 0.71+1.97 us)  : 0.66 to 0.70 s
  MAP_POPULATE (1.05 to 1.13 us)                                : 0.28 to 0.30 s
  2 MB pages: 512 faults x 280 to 370 us                           : 0.14 to 0.19 s
```

So 262,144 faults and about a third of a second. Reading first costs twice as much (the read maps the shared zero page, the write then breaks it). `MAP_POPULATE` costs about the same *in total* but moves it into the `mmap` call, before the first request. Huge pages take 512 faults and roughly 0.14 to 0.19 s when they are available; tutorial 15 saw single faults of 1.5 to 2 ms, which would make it 0.8 to 1.0 s.

**(b)** `1,024 × 1.14 to 1.32 µs` is **1.17 to 1.35 ms** per request in faults alone (plus the `mmap`/`munmap`, 3.5 to 6.4 µs). One core can therefore serve at most 740 to 850 such requests a second, and at 1,000 a second the faults alone would need 117 to 135 % of a core. Reusing the buffer removes the faults. Whether `malloc` already does that for you depends on its mmap threshold (section 15): check with `strace -e trace=brk,mmap,munmap`.

**(c)** `10,000 × 0.78 to 1.19 ms` is **8 to 12 seconds** of process creation, loading and teardown, before the program does any work. Remove it by not starting a process per item (let one process handle all the inputs, or batch with `xargs -n 1000`), and, if you must, link statically or use `posix_spawn`.

**(d)** (1) The allocator kept it: a freed block goes back to a bin or the thread cache, not to the kernel. Check with `malloc_stats()` / `mallinfo2()`, and `strace -e trace=brk,munmap,madvise` to see whether the kernel was ever told. (2) The block sits below a live allocation in the heap, so the top of the heap cannot be trimmed. Check with the heap layout in `/proc/PID/smaps` and the allocator statistics. (3) It was never resident: the resident size you were comparing was taken before the first touch (section 15), so freeing it changes nothing. Check `RssAnon` before and after the touch.

**(e)** The three programs, and what they printed here:

```c
// faults.c: how many page faults does it take to touch 4 MiB (1024 pages) in different ways, and what do they cost?
//   gcc -O2 -o faults faults.c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/resource.h>

#define PAGES 1024
#define LEN (PAGES * 4096UL)
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }
static long minf(void) { struct rusage r; getrusage(RUSAGE_SELF, &r); return r.ru_minflt; }
static long majf(void) { struct rusage r; getrusage(RUSAGE_SELF, &r); return r.ru_majflt; }

static void touch(const char *what, volatile char *p, int write) {
    long m0 = minf(), j0 = majf(); double t0 = now();
    for (unsigned long i = 0; i < LEN; i += 4096) { if (write) p[i] = 1; else (void)p[i]; }
    double t1 = now();
    printf("  %-44s %5ld minor, %3ld major faults, %6.0f ns per page\n", what, minf() - m0, majf() - j0, (t1 - t0) / PAGES);
}
int main(void) {
    // 1. anonymous memory
    char *a = mmap(NULL, LEN, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    madvise(a, LEN, MADV_NOHUGEPAGE);
    touch("anonymous, first write to each page", a, 1);
    touch("same pages, second write", a, 1);
    char *b = mmap(NULL, LEN, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    madvise(b, LEN, MADV_NOHUGEPAGE);
    touch("anonymous, first READ of each page", b, 0);
    touch("  ... then first write to the same pages", b, 1);
    double t0 = now(); long m0 = minf();
    char *c = mmap(NULL, LEN, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
    double t1 = now(); long m1 = minf();
    printf("  %-44s %5ld minor faults, mmap call took %.0f ns per page\n", "anonymous with MAP_POPULATE (mmap itself)", m1 - m0, (t1 - t0) / PAGES);
    touch("  ... then touching it", c, 1);
    // 2. a file that is in the page cache
    int fd = open("filedata.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);
    char *buf = malloc(LEN); memset(buf, 7, LEN); if (write(fd, buf, LEN) != (ssize_t)LEN) return 1; fsync(fd);
    char *f = mmap(NULL, LEN, PROT_READ, MAP_PRIVATE, fd, 0);
    touch("file in page cache, read each page", f, 0);
    // 3. the same file after dropping it from the cache
    char *g = mmap(NULL, LEN, PROT_READ, MAP_PRIVATE, fd, 0);
    posix_fadvise(fd, 0, LEN, POSIX_FADV_DONTNEED);
    munmap(f, LEN);
    char *h = mmap(NULL, LEN, PROT_READ, MAP_SHARED, fd, 0);
    posix_fadvise(fd, 0, LEN, POSIX_FADV_DONTNEED);
    touch("file after POSIX_FADV_DONTNEED, read each page", h, 0);
    (void)g;
    return 0;
}
```

```text
run 1
  anonymous, first write to each page           1027 minor,   0 major faults,   1136 ns per page
  same pages, second write                         0 minor,   0 major faults,      7 ns per page
  anonymous, first READ of each page            1024 minor,   0 major faults,    712 ns per page
    ... then first write to the same pages      1024 minor,   0 major faults,   1958 ns per page
  anonymous with MAP_POPULATE (mmap itself)     1024 minor faults, mmap call took 1133 ns per page
    ... then touching it                           0 minor,   0 major faults,     12 ns per page
  file in page cache, read each page               2 minor,   0 major faults,     11 ns per page
  file after POSIX_FADV_DONTNEED, read each page    36 minor,   1 major faults,   1530 ns per page
run 2
  anonymous, first write to each page           1027 minor,   0 major faults,   1323 ns per page
  same pages, second write                         0 minor,   0 major faults,     11 ns per page
  anonymous, first READ of each page            1024 minor,   0 major faults,    699 ns per page
    ... then first write to the same pages      1024 minor,   0 major faults,   1970 ns per page
  anonymous with MAP_POPULATE (mmap itself)     1024 minor faults, mmap call took 1106 ns per page
    ... then touching it                           0 minor,   0 major faults,     11 ns per page
  file in page cache, read each page               2 minor,   0 major faults,     10 ns per page
  file after POSIX_FADV_DONTNEED, read each page    36 minor,   1 major faults,   3634 ns per page
run 3
  anonymous, first write to each page           1027 minor,   0 major faults,   1214 ns per page
  same pages, second write                         0 minor,   0 major faults,      7 ns per page
  anonymous, first READ of each page            1024 minor,   0 major faults,    606 ns per page
    ... then first write to the same pages      1024 minor,   0 major faults,   1909 ns per page
  anonymous with MAP_POPULATE (mmap itself)     1024 minor faults, mmap call took 1051 ns per page
    ... then touching it                           0 minor,   0 major faults,      8 ns per page
  file in page cache, read each page               2 minor,   0 major faults,     17 ns per page
  file after POSIX_FADV_DONTNEED, read each page    36 minor,   1 major faults,   1403 ns per page
```

```c
// mallocwalk.c: from malloc() to a physical page: where the memory comes from, when it becomes real, and what the costs are.
//   gcc -O2 -fno-builtin -o mallocwalk mallocwalk.c         ./mallocwalk        (and  strace -e trace=brk,mmap,munmap ./mallocwalk)
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <malloc.h>
#include <sys/mman.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }
static long rss_kb(void) { char l[128]; long v = -1; FILE *f = fopen("/proc/self/status", "r"); while (fgets(l, sizeof l, f)) if (sscanf(l, "RssAnon: %ld", &v) == 1) break; fclose(f); return v; }
#define KEEP(p) __asm__ volatile("" :: "r"(p) : "memory")   // the compiler must assume p escapes, so it cannot drop the malloc/free pair
static uint64_t frame(int pm, void *p) {            // physical frame number of the page holding p (0 if absent or hidden)
    uint64_t e = 0; pread(pm, &e, 8, ((uintptr_t)p / 4096) * 8);
    return (e >> 63) ? (e & ((1ULL << 55) - 1)) : 0;
}
int main(void) {
    int pm = open("/proc/self/pagemap", O_RDONLY);
    fprintf(stderr, "--- a small block\n");
    char *s = malloc(100);
    fprintf(stderr, "malloc(100)      = %p  (heap, brk end %p)\n", s, sbrk(0));
    fprintf(stderr, "--- 1 MiB\n");
    char *big = malloc(1 << 20);
    fprintf(stderr, "malloc(1 MiB)    = %p  anonymous RSS %ld KB before touching\n", big, rss_kb());
    for (int i = 0; i < (1 << 20); i += 4096) big[i] = 1;
    fprintf(stderr, "                   anonymous RSS %ld KB after touching one byte per page\n", rss_kb());
    fprintf(stderr, "physical frames of pages 0..5 of the block (frame number x 4096):");
    for (int i = 0; i < 6; i++) fprintf(stderr, " %#llx", (unsigned long long)frame(pm, big + i * 4096) * 4096);
    fprintf(stderr, "\n");
    free(big);
    char *big2 = malloc(1 << 20);
    fprintf(stderr, "free, then malloc(1 MiB) again = %p  (%s)\n", big2, ((uintptr_t)big2 > (uintptr_t)s && (uintptr_t)big2 < 0x7f0000000000ULL) ? "now from the brk heap: the threshold grew" : "mmap again");
    free(big2);
    // costs
    enum { N = 200000 };
    double t0 = now();
    for (int i = 0; i < N; i++) { char *p = malloc(64); KEEP(p); p[0] = 1; KEEP(p); free(p); }
    double t1 = now();
    printf("malloc(64) + write + free              : %6.1f ns\n", (t1 - t0) / N);
    t0 = now();
    for (int i = 0; i < 2000; i++) { char *p = malloc(1 << 20); KEEP(p); p[0] = 1; KEEP(p); free(p); }
    t1 = now();
    printf("malloc(1 MiB) + write + free, heap memory reused : %6.0f ns (no system call: the free space is already resident)\n", (t1 - t0) / 2000);
    t0 = now();
    for (int i = 0; i < 2000; i++) { char *p = mmap(NULL, 1 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); p[0] = 1; munmap(p, 1 << 20); }
    t1 = now();
    printf("mmap(1 MiB) + write 1 page + munmap              : %6.0f ns (what a large malloc/free costs when the kernel is involved)\n", (t1 - t0) / 2000);
    t0 = now();
    for (int i = 0; i < 200; i++) { char *p = mmap(NULL, 1 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); for (int k = 0; k < (1 << 20); k += 4096) p[k] = 1; munmap(p, 1 << 20); }
    t1 = now();
    printf("mmap(1 MiB) + write all 256 pages + munmap       : %6.0f ns = %.2f us per page\n", (t1 - t0) / 200, (t1 - t0) / 200 / 256 / 1e3);
    return 0;
}
```

```text
$ sudo ./mallocwalk            (stderr and stdout together; one run, the ranges in section 15 are over five)
--- a small block
malloc(100)      = 0x5630a1b7c2a0  (heap, brk end 0x5630a1b9d000)
--- 1 MiB
malloc(1 MiB)    = 0x7fdc38aff010  anonymous RSS 128 KB before touching
                   anonymous RSS 1148 KB after touching one byte per page
physical frames of pages 0..5 of the block (frame number x 4096): 0x11b4d9000 0x13a47d000 0x122312000 0x126263000 0x126923000 0x11bb39000
free, then malloc(1 MiB) again = 0x5630a1b7c900  (now from the brk heap: the threshold grew)
malloc(64) + write + free              :   13.9 ns
malloc(1 MiB) + write + free, heap memory reused :     33 ns (no system call: the free space is already resident)
mmap(1 MiB) + write 1 page + munmap              :   5982 ns (what a large malloc/free costs when the kernel is involved)
mmap(1 MiB) + write all 256 pages + munmap       : 293656 ns = 1.15 us per page

$ strace -e trace=brk,mmap,munmap ./mallocwalk 2>&1 | grep -E 'brk|1052672|munmap' | head -9     (a separate run, so the addresses differ)
brk(NULL)                               = 0x5605ade68000
munmap(0x7f95b010e000, 55595)           = 0
brk(NULL)                               = 0x5605ade68000
brk(0x5605ade89000)                     = 0x5605ade89000
malloc(100)      = 0x5605ade682a0  (heap, brk end 0x5605ade89000)
mmap(NULL, 1052672, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x7f95afcff000
munmap(0x7f95afcff000, 1052672)         = 0
brk(0x5605adf89000)                     = 0x5605adf89000
free, then malloc(1 MiB) again = 0x5605ade68900  (now from the brk heap: the threshold grew)
```

```c
// spawn.c: time N fork + execve + wait cycles of a program and count the page faults the child took.
//   gcc -O2 -o spawn spawn.c      ./spawn ./hello_dyn 500
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/resource.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }
int main(int argc, char **argv) {
    int n = atoi(argv[2]);
    int devnull = open("/dev/null", O_WRONLY);
    struct rusage r0, r1; getrusage(RUSAGE_CHILDREN, &r0);
    double t0 = now();
    for (int i = 0; i < n; i++) {
        pid_t p = fork();
        if (p == 0) { dup2(devnull, 1); char *args[] = {argv[1], NULL}; execv(argv[1], args); _exit(127); }
        waitpid(p, NULL, 0);
    }
    double t1 = now(); getrusage(RUSAGE_CHILDREN, &r1);
    printf("%-14s %7.0f us per fork+exec+wait, %5.1f minor page faults per run\n", argv[1], (t1 - t0) / n / 1e3, (double)(r1.ru_minflt - r0.ru_minflt) / n);
    return 0;
}
```

```text
./hello_static     932 us per fork+exec+wait,  46.5 minor page faults per run
./hello_dyn       1045 us per fork+exec+wait,  75.5 minor page faults per run
/bin/true          888 us per fork+exec+wait,  71.5 minor page faults per run
./hello_static     784 us per fork+exec+wait,  46.5 minor page faults per run
./hello_dyn        922 us per fork+exec+wait,  75.4 minor page faults per run
/bin/true          922 us per fork+exec+wait,  72.5 minor page faults per run
./hello_static     919 us per fork+exec+wait,  46.5 minor page faults per run
./hello_dyn       1100 us per fork+exec+wait,  75.5 minor page faults per run
/bin/true         1192 us per fork+exec+wait,  71.5 minor page faults per run
```

Build lines: `gcc -O2 -o faults faults.c`; `gcc -O2 -fno-builtin -o mallocwalk mallocwalk.c` (run as root for the frame numbers, and under `strace -e trace=brk,mmap,munmap` for the system calls); `gcc -O2 -o spawn spawn.c` with the three test binaries built from `hello.c` (`gcc -O2 -o hello_dyn hello.c`, `gcc -O2 -static -o hello_static hello.c`). Differences of more than a factor of two usually come from the fault cost (the hypervisor supplying memory on first use, huge pages enabled, a different page size), or from a first touch that the host also had to back.

### Problem D

`budget.py`, the check for Problems C and D, with its output for D:

```python
# budget.py: the arithmetic of Problems C and D. Inputs are numbers measured in tutorials 14 to 31 (marked "measured") or ASSUMED.
GiB = 1 << 30
pages = GiB // 4096
print(f"== Problem C: first touch of a 1 GiB buffer = {pages:,} pages of 4 KiB")
for name, lo, hi in (("one write per page (1.14 to 1.32 us)", 1.14, 1.32),
                     ("read each page first, then write (0.61+1.91 to 0.71+1.97 us)", 0.61 + 1.91, 0.71 + 1.97),
                     ("MAP_POPULATE (1.05 to 1.13 us)", 1.05, 1.13)):
    print(f"  {name:62s}: {pages * lo / 1e6:.2f} to {pages * hi / 1e6:.2f} s")
huge = GiB // (2 << 20)
print(f"  2 MB pages: {huge} faults x 280 to 370 us{'':27s}: {huge * 280 / 1e6:.2f} to {huge * 370 / 1e6:.2f} s")
print(f"  a 4 MiB buffer mapped fresh for every request (1,024 pages x 1.14 to 1.32 us): {1024 * 1.14 / 1e3:.2f} to {1024 * 1.32 / 1e3:.2f} ms")
print(f"  10,000 runs of a ~1 ms fork+exec+wait (0.78 to 1.19 ms measured): {10000 * 0.78 / 1e3:.1f} to {10000 * 1.19 / 1e3:.1f} s")

print("\n== Problem D: the packet budget")
for gbps in (10, 25, 100):
    wire_bytes = 64 + 8 + 12                      # frame + preamble/start delimiter + inter-frame gap
    pps = gbps * 1e9 / (wire_bytes * 8)
    print(f"  {gbps:3d} GbE, 64-byte frames: {pps / 1e6:6.2f} Mpps = {1e9 / pps:5.1f} ns per packet = {3 * 1e9 / pps:4.0f} cycles at 3 GHz")
budget = 1e9 / (10e9 / (84 * 8))
print(f"\n  per-event cost at 10 GbE (budget {budget:.1f} ns per packet):  [measured unless marked]")
for name, ns in (("raw system call (measured 115 to 126 ns)", 120),
                 ("context switch, one way (measured 1.2 to 1.5 us)", 1350),
                 ("a VM exit, cpuid (measured 6.9 to 7.8 us)", 7350),
                 ("waking a sleeping thread on another CPU (measured ~10 us)", 10000)):
    print(f"    {name:62s}: {ns:6.0f} ns = {ns / budget:6.1f} packets' worth")
work = 30      # ASSUMED: ns of real work per packet in a tight polling loop
event = 5000   # ASSUMED: ns per interrupt, including the top half and the switch to the poll
print(f"\n  ASSUMED: {work} ns of work per packet, {event} ns per interrupt")
need = event / (budget - work)
print(f"    interrupt cost per packet = {event} / B; it must fit in {budget - work:.1f} ns, so B >= {need:.0f} packets per interrupt")
print(f"    measured at low load: 1.7 packets per interrupt -> {event / 1.7:.0f} ns per packet from the interrupt alone")
print(f"    ASSUMED 1 us per packet through the kernel stack: {1e9 / 1000 / 1e6:.1f} Mpps per core, {14.88 / 1.0:.1f} cores for 14.88 Mpps")
print(f"    ASSUMED 50 ns per packet with kernel bypass: {1e9 / 50 / 1e6:.0f} Mpps per core, one core is enough")
```

```text
== Problem C: first touch of a 1 GiB buffer = 262,144 pages of 4 KiB
  one write per page (1.14 to 1.32 us)                          : 0.30 to 0.35 s
  read each page first, then write (0.61+1.91 to 0.71+1.97 us)  : 0.66 to 0.70 s
  MAP_POPULATE (1.05 to 1.13 us)                                : 0.28 to 0.30 s
  2 MB pages: 512 faults x 280 to 370 us                           : 0.14 to 0.19 s
  a 4 MiB buffer mapped fresh for every request (1,024 pages x 1.14 to 1.32 us): 1.17 to 1.35 ms
  10,000 runs of a ~1 ms fork+exec+wait (0.78 to 1.19 ms measured): 7.8 to 11.9 s

== Problem D: the packet budget
   10 GbE, 64-byte frames:  14.88 Mpps =  67.2 ns per packet =  202 cycles at 3 GHz
   25 GbE, 64-byte frames:  37.20 Mpps =  26.9 ns per packet =   81 cycles at 3 GHz
  100 GbE, 64-byte frames: 148.81 Mpps =   6.7 ns per packet =   20 cycles at 3 GHz

  per-event cost at 10 GbE (budget 67.2 ns per packet):  [measured unless marked]
    raw system call (measured 115 to 126 ns)                      :    120 ns =    1.8 packets' worth
    context switch, one way (measured 1.2 to 1.5 us)              :   1350 ns =   20.1 packets' worth
    a VM exit, cpuid (measured 6.9 to 7.8 us)                     :   7350 ns =  109.4 packets' worth
    waking a sleeping thread on another CPU (measured ~10 us)     :  10000 ns =  148.8 packets' worth

  ASSUMED: 30 ns of work per packet, 5000 ns per interrupt
    interrupt cost per packet = 5000 / B; it must fit in 37.2 ns, so B >= 134 packets per interrupt
    measured at low load: 1.7 packets per interrupt -> 2941 ns per packet from the interrupt alone
    ASSUMED 1 us per packet through the kernel stack: 1.0 Mpps per core, 14.9 cores for 14.88 Mpps
    ASSUMED 50 ns per packet with kernel bypass: 20 Mpps per core, one core is enough
```

**(a)** 10 GbE: 14.88 Mpps, 67.2 ns, 202 cycles. 25 GbE: 37.20 Mpps, 26.9 ns, 81 cycles. 100 GbE: 148.81 Mpps, 6.7 ns, 20 cycles. The budget shrinks by the same factor as the line rate grows, and at 100 GbE twenty cycles is less than one cache miss.

**(b)** A raw system call is 120 ns, 1.8 packets' worth: even with no other work a core making one call per packet tops out at `1 / 120 ns = 8.3 Mpps`, below the 14.88 Mpps required. A one-way context switch is 1.2 to 1.5 µs (about 20 packets' worth), a VM exit about 7.35 µs (109) and a cross-CPU wake-up about 10 µs (149). So nothing in that list can happen once per packet; each must be amortised over a batch, or removed from the path.

**(c)** With 5 µs per interrupt (assumed) and 30 ns of work (assumed), `5000 / B + 30 ≤ 67.2` gives `B ≥ 134` packets per interrupt. At low load the measured figure was 1.7, which is `5000 / 1.7 = 2,941 ns` per packet from the interrupt alone, far over the budget. The design works only because **NAPI** masks the interrupt while the poll loop runs: the busier the link, the more packets each poll finds, and the batch grows by itself (tutorial 26 section 4.5). Interrupt moderation (tutorial 26 section 4.4) does the same in hardware by delaying the interrupt.

**(d)** At 1 µs per packet (assumed) one core handles 1 Mpps, so 14.88 Mpps needs about 15 cores. At 50 ns per packet (assumed) one core handles 20 Mpps, so one polling core is enough, at the price of burning 100 % of it. In this VM there is an extra limit: the virtio device offers a single queue pair (tutorial 26 section 8.6), so all receive work is on one core however many are free, and receive-side scaling has nothing to spread across.

**(e)** Measured in this series: the system call, context switch, VM exit and wake-up times; the 1.7 packets per interrupt and the receive size of about 6 to 7 KB. Computed exactly: the 14.88, 37.2 and 148.8 Mpps. **Assumed:** 5 µs per interrupt, 30 ns of work per packet, 1 µs per packet through the kernel stack, 50 ns per packet in user space. I would measure the per-packet kernel cost first (it decides the core count), with `perf` on a machine that has it; this VM has no counters.

### The programs behind Part II

The remaining programs, with their run lines. All were run in this VM; the Arm one under QEMU.

`pingpong.c` (section 13), `gcc -O2 -pthread -mavx512f -o pingpong pingpong.c`, run as `./pingpong proc|thread none|ymm|zmm` (it pins itself to CPU 0; the `zmm` rows need a CPU with AVX-512):

```c
// pingpong.c: cost of a context switch: two tasks on ONE CPU hand a byte back and forth through two pipes.
//   gcc -O2 -pthread -o pingpong pingpong.c      ./pingpong proc|thread none|ymm|zmm
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>
#include <pthread.h>
#include <sys/wait.h>

#define ROUNDS 200000
static int p1[2], p2[2];            // parent -> child, child -> parent
static int vec;                     // 0 none, 1 ymm, 2 zmm: how much vector state is live at each switch

__attribute__((noinline)) static void dirty(void) {
    if (vec == 1) {
        __asm__ volatile("vpcmpeqd %%ymm0,%%ymm0,%%ymm0\n vpcmpeqd %%ymm1,%%ymm1,%%ymm1\n vpcmpeqd %%ymm2,%%ymm2,%%ymm2\n vpcmpeqd %%ymm3,%%ymm3,%%ymm3" ::: "ymm0", "ymm1", "ymm2", "ymm3");
    } else if (vec == 2) {          // write ones into every zmm register, 0 to 31
        __asm__ volatile(
            "vpternlogd $0xff,%%zmm0,%%zmm0,%%zmm0\n vpternlogd $0xff,%%zmm1,%%zmm1,%%zmm1\n vpternlogd $0xff,%%zmm2,%%zmm2,%%zmm2\n vpternlogd $0xff,%%zmm3,%%zmm3,%%zmm3\n"
            "vpternlogd $0xff,%%zmm16,%%zmm16,%%zmm16\n vpternlogd $0xff,%%zmm17,%%zmm17,%%zmm17\n vpternlogd $0xff,%%zmm30,%%zmm30,%%zmm30\n vpternlogd $0xff,%%zmm31,%%zmm31,%%zmm31"
            ::: "zmm0", "zmm1", "zmm2", "zmm3", "zmm16", "zmm17", "zmm30", "zmm31");
    }
}
static void *child(void *arg) {
    char c;
    for (int i = 0; i < ROUNDS; i++) { dirty(); if (read(p1[0], &c, 1) != 1) break; dirty(); if (write(p2[1], &c, 1) != 1) break; }
    return arg;
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }

int main(int argc, char **argv) {
    int threads = argc > 1 && !strcmp(argv[1], "thread");
    vec = argc > 2 ? (!strcmp(argv[2], "ymm") ? 1 : !strcmp(argv[2], "zmm") ? 2 : 0) : 0;
    cpu_set_t s; CPU_ZERO(&s); CPU_SET(0, &s); sched_setaffinity(0, sizeof s, &s);   // children inherit it
    pipe(p1); pipe(p2);
    pthread_t th; pid_t pid = 0;
    if (threads) pthread_create(&th, NULL, child, NULL); else if ((pid = fork()) == 0) { child(NULL); _exit(0); }
    char c = 'x';
    double t0 = now();
    for (int i = 0; i < ROUNDS; i++) { dirty(); write(p1[1], &c, 1); dirty(); read(p2[0], &c, 1); }
    double t1 = now();
    if (threads) pthread_join(th, NULL); else waitpid(pid, NULL, 0);
    double rt = (t1 - t0) / ROUNDS;
    printf("%-6s %-4s : %6.0f ns per round trip = %5.0f ns per one-way hand-off (a write, a switch, a read)\n", threads ? "thread" : "proc", argv[2] ? argv[2] : "none", rt, rt / 2);
    return 0;
}
```

```text
proc   none :   2949 ns per round trip =  1475 ns per one-way hand-off (a write, a switch, a read)
proc   ymm  :   3083 ns per round trip =  1541 ns per one-way hand-off (a write, a switch, a read)
proc   zmm  :   2580 ns per round trip =  1290 ns per one-way hand-off (a write, a switch, a read)
thread none :   2900 ns per round trip =  1450 ns per one-way hand-off (a write, a switch, a read)
thread ymm  :   2704 ns per round trip =  1352 ns per one-way hand-off (a write, a switch, a read)
thread zmm  :   2759 ns per round trip =  1379 ns per one-way hand-off (a write, a switch, a read)

proc   none :   2954 ns per round trip =  1477 ns per one-way hand-off (a write, a switch, a read)
proc   ymm  :   2517 ns per round trip =  1259 ns per one-way hand-off (a write, a switch, a read)
proc   zmm  :   2617 ns per round trip =  1308 ns per one-way hand-off (a write, a switch, a read)
thread none :   2476 ns per round trip =  1238 ns per one-way hand-off (a write, a switch, a read)
thread ymm  :   2443 ns per round trip =  1221 ns per one-way hand-off (a write, a switch, a read)
thread zmm  :   2731 ns per round trip =  1365 ns per one-way hand-off (a write, a switch, a read)

proc   none :   2445 ns per round trip =  1223 ns per one-way hand-off (a write, a switch, a read)
proc   ymm  :   2553 ns per round trip =  1276 ns per one-way hand-off (a write, a switch, a read)
proc   zmm  :   2751 ns per round trip =  1375 ns per one-way hand-off (a write, a switch, a read)
thread none :   2788 ns per round trip =  1394 ns per one-way hand-off (a write, a switch, a read)
thread ymm  :   2468 ns per round trip =  1234 ns per one-way hand-off (a write, a switch, a read)
thread zmm  :   2447 ns per round trip =  1224 ns per one-way hand-off (a write, a switch, a read)
```

The two system-call programs of section 12 (`gcc -nostdlib -static -o sys_x86 sys_x86.S && ./sys_x86`; `aarch64-linux-gnu-gcc -nostdlib -static -o sys_a64 sys_a64.S && qemu-aarch64 ./sys_a64`; `strace ./sys_x86`):

```text
# sys_x86.S: write(1, msg, 6); exit(0) with the raw syscall instruction.   gcc -nostdlib -static -o sys_x86 sys_x86.S
        .globl _start
        .text
_start: mov     $1, %eax            # syscall number: write
        mov     $1, %edi            # arg 1: fd
        lea     msg(%rip), %rsi     # arg 2: buffer
        mov     $6, %edx            # arg 3: length
        syscall                     # enter the kernel; the CPU puts the return address in rcx and rflags in r11
        mov     $60, %eax           # exit
        xor     %edi, %edi
        syscall
        .section .rodata
msg:    .ascii  "hello\n"
```

```text
// sys_a64.S: write(1, msg, 6); exit(0) with svc.   aarch64-linux-gnu-gcc -nostdlib -static -o sys_a64 sys_a64.S
        .globl _start
        .text
_start: mov     x8, #64             // syscall number: write (AArch64 numbering differs from x86-64)
        mov     x0, #1              // arg 1: fd
        adr     x1, msg             // arg 2: buffer
        mov     x2, #6              // arg 3: length
        svc     #0                  // exception to EL1; the return address goes to ELR_EL1
        mov     x8, #93             // exit
        mov     x0, #0
        svc     #0
        .section .rodata
msg:    .ascii  "hello\n"
```

The network counters of section 9, which print the packet, interrupt and softirq counts of `eth0` (download any file of a few hundred KB and compare the before and after lines):

```bash
#!/bin/bash
# netsnap.sh: print eth0 packet counters, the interrupt count of the virtio-net receive/transmit vectors, and the NET_RX/NET_TX softirq counts
irq=$(readlink /sys/class/net/eth0/device | sed 's#.*/##')            # e.g. virtio7
rxp=$(awk '/eth0:/{print $3}' /proc/net/dev); txp=$(awk '/eth0:/{print $11}' /proc/net/dev)
rxb=$(awk '/eth0:/{print $2}' /proc/net/dev)
rxi=$(awk -v n="$irq-input" '$0 ~ n {s=0; for(i=2;i<=3;i++) s+=$i; print s}' /proc/interrupts)
txi=$(awk -v n="$irq-output" '$0 ~ n {s=0; for(i=2;i<=3;i++) s+=$i; print s}' /proc/interrupts)
nrx=$(awk '/NET_RX/{print $2+$3}' /proc/softirqs); ntx=$(awk '/NET_TX/{print $2+$3}' /proc/softirqs)
echo "$rxp $rxb $txp $rxi $txi $nrx $ntx"
```

```python
import sys
a = list(map(int, sys.argv[1].split())); b = list(map(int, sys.argv[2].split()))
d = [y - x for x, y in zip(a, b)]
names = ["rx packets", "rx bytes", "tx packets", "rx interrupts", "tx interrupts", "NET_RX softirqs", "NET_TX softirqs"]
print("  " + ", ".join(f"{n} {v}" for n, v in zip(names, d)))
if d[0]: print(f"  bytes per rx packet {d[1]/d[0]:.0f}; rx packets per rx interrupt {d[0]/max(d[3],1):.1f}; rx packets per NET_RX softirq {d[0]/max(d[5],1):.1f}")
```

```text
B=$(./netsnap.sh); curl -sL -o /dev/null https://files.pythonhosted.org/packages/source/z/zstandard/zstandard-0.25.0.tar.gz; A=$(./netsnap.sh); python3 netdelta.py "$B" "$A"
```

A fourth run, after the three in section 9, gave 99 packets, 7,336 bytes per packet, 54 receive interrupts (1.8 packets per interrupt). The `dmesg` output of section 11 is `dmesg | head -400`; the loader statistics of section 8 are `LD_DEBUG=statistics ./hello_dyn`.

---

## Cheat sheet

- **Arm against x86:** the label does not decide speed or efficiency (microarchitecture does). The real differences: memory model (TSO against weak), encoding and registers (variable against fixed 4 bytes; 16 against 31 registers), what C leaves undefined, system architecture (rings and MSRs against exception levels; APIC against GIC; reset vector and firmware against a trusted-firmware chain and a device tree) and vector extensions.
- **Density, measured on 53,692 lines of zstd:** `.text` 567,576 bytes (x86-64) against 482,028 (AArch64) at `-O2`, 367,234 against 330,052 at `-Os`; instructions 141,447 against 120,507; bytes per instruction 4.01 against 4.00; stack accesses 19.9 % against 10.5 % of instructions. One small loop (tutorial 06) went the other way.
- **Ordering and atomics:** `release`/`acquire` are plain `mov` on x86 and `stlr`/`ldar` on Arm; identical x86 code means an x86 test cannot fail; QEMU does not reorder either. Procedure: audit by hand, ThreadSanitizer, read the disassembly, model-check, test on the weakest hardware, never a blanket `dmb`. Atomic read-modify-write: `lock xadd` against `ldaddal` (armv8.1) or an `ldaxr`/`stlxr` loop, or the outline-atomics call.
- **Porting traps:** `char` signedness, `long double` (64 against 113 bits), divide by zero and `INT_MIN / -1`, float to integer, `fmadd` contraction (`-ffp-contract=off`), misaligned atomics, instruction-cache maintenance for generated code, the page size.
- **x86 specifics:** `cpuid` exits to the hypervisor (about 7.5 µs); `XSAVE` area 11,008 bytes here (AMX tile data 8 KB), minimum signal stack 11,952 bytes. Arm SVE state is about `34 × VL` bytes (2,184 at 512 bits).
- **The ledger:** L1 0.4 to 1.3 ns; other core's cache 56 to 102 ns; DRAM 146 to 149 ns (221 with a TLB miss); system call 115 to 126 ns; `malloc`/`free` 8 to 15 ns; minor fault 1.1 to 1.3 µs; context switch 1.2 to 1.5 µs; `cpuid` 7.5 µs; cross-CPU wake-up 10 µs; huge-page fault 280 to 370 µs; `fork`+`exec` about 1 ms (75 faults dynamic, 46 static); major fault 1.4 to 3.7 ms for 4 MiB.
- **Page faults:** write first, 1.14 to 1.32 µs per page; read first then write 2.5 to 2.7 µs; `MAP_POPULATE` 1.05 to 1.13 µs (in the call); a cached file 2 faults for 1,024 pages; a cold file 1 major fault and 36 minor, 1.4 to 3.7 ms.
- **`malloc`:** thread cache, then `brk` (132 KiB first), then `mmap` at 128 KiB and up (threshold rises after a free); resident only at the first touch; frames scattered; `free` rarely returns memory.
- **Packets:** 64-byte frames are 14.88 Mpps at 10 GbE, 67 ns each; batch with NAPI and aggregation or poll from user space; the virtio device here has one queue pair, receive packets of about 6 to 7 KB and about 1.7 packets per interrupt at low load.
- **Boot:** x86 reset vector `0xFFFFFFF0`, firmware (DRAM training, PCIe, ACPI), bootloader, kernel, `init`; Arm boot ROM, trusted-firmware stages (BL31 stays at EL3), kernel with a device tree; a micro-VM skips the firmware: 0.975 s from the kernel's first line to the first user-space program here.
- **How I know:** measured in this VM (Intel Xeon under KVM, Linux 6.18, GCC 13.3, `perf` and MSRs unavailable): the code-density, ordering, pitfall, `xstate`, process-creation, page-fault, `malloc`, context-switch and packet-counter results and the `dmesg` timeline. Arm code was compiled with `aarch64-linux-gnu-gcc` 13.3, disassembled, and run under `qemu-aarch64`, which checks results and not timing or ordering; **I have no Arm hardware, no Arm timings and no Arm ordering result.** From published sources: the Linux kernel documentation for the arm64 memory layout and SVE, and Blem et al., HPCA 2013, for the microarchitecture-against-ISA finding; the older note's Arm-machine numbers are quoted as not re-run. **Standard knowledge I did not check against a manual:** the x86 reset vector, the Arm trusted-firmware stage names, the exception class and vector offset of `svc`, glibc's use of `cpuid` for `ifunc`, message-signalled interrupt routing on Arm, the TrustZone and pointer-authentication details of tutorial 30, the 16 KB granule, split-lock behaviour, `XSAVEOPT`/`XSAVEC` use, herd7 as a model checker, and the masking of the system-call table index. Items marked *assumed* in Problem D are numbers I chose, not measured. **I wrote no exploit.**

**Next:** the Computer Organization track is complete (tutorials 01 to 31). The next track is in `../00_MASTER_PLAN.md`; say "next" and I will start it.
