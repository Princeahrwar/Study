# CO 07: Assembly in Practice (x86-64 and AArch64)

*Part of the [Computer Organization plan](00_plan.md) · Core tutorial · Time: about 3 hours (1.5 reading, 1.5 problems) · You need: [tutorial 06](06_isa_design.md) (registers, addressing modes, RISC vs CISC) and [tutorial 02](02_data_representation.md) (two's complement, carry vs overflow)*

By the end of this tutorial you can:

- read x86-64 and AArch64 assembly and tell Intel from AT&T syntax;
- name the registers and say which ones carry arguments, results and saved values;
- work out the flags after a `cmp` or `add` and choose the right signed or unsigned condition;
- follow the calling conventions (System V on x86-64, AAPCS64 on AArch64) and draw a stack frame, including alignment and the red zone;
- read compiler output at `-O0`, `-O1` and `-O2` and map it back to the C source;
- write a small piece of inline assembly correctly;
- explain what the assembler, linker and loader each do, and what a relocation is.

Every listing is real output from `clang 18` (with `-fno-asynchronous-unwind-tables` to remove noise), unless marked schematic. Flag values and register behaviour were checked by running small programs on x86-64 and on AArch64 (under an emulator). Your compiler may differ by a few instructions; the patterns will be the same.

---

## 1. Tools and syntax

You learn assembly by compiling small C functions and reading the result.

```sh
clang -O1 -S -masm=intel -fno-asynchronous-unwind-tables f.c -o -       # x86-64, Intel syntax, to the terminal
clang --target=aarch64-linux-gnu -O1 -S f.c -o -                        # AArch64 listing, from any machine
objdump -d -M intel --no-show-raw-insn a.out                            # disassemble a built binary
```

[godbolt.org](https://godbolt.org) does the same in a browser for many compilers and targets side by side; tick "Compile to binary object" to see the bytes.

On an Apple Silicon Mac, plain `clang -O1 -S f.c -o -` already produces AArch64, but with Apple's twists: symbol names start with an underscore, globals use `adrp x8, _g@PAGE`, and the binary format is Mach-O instead of ELF (use `otool -tV a.out` instead of `objdump`). Linux, including embedded Linux, uses ELF, so use godbolt or a Linux machine when you want the Linux picture.

**Operand order.** On both ISAs in the Intel style the **destination comes first**: `mov eax, ebx` copies ebx into eax, and `add x0, x1, x2` means `x0 = x1 + x2`. Stores are the exception on AArch64: `str w0, [x1]` writes w0 to memory.

**AT&T syntax** is what GNU tools print by default for x86 (unless you pass `-masm=intel`). It reverses the operands and decorates everything:

| Intel | AT&T |
|---|---|
| `mov eax, DWORD PTR [rdi+rcx*4+16]` | `movl 16(%rdi,%rcx,4), %eax` |
| `add eax, 5` | `addl $5, %eax` |
| `lea rax, [rdi+rdi*4]` | `leaq (%rdi,%rdi,4), %rax` |
| `movsxd rax, edi` | `movslq %edi, %rax` |
| `cmp edi, esi` | `cmpl %esi, %edi` |

(I assembled these and the bytes were identical; other rows work the same way: `mov DWORD PTR [rdi], 7` is `movl $7, (%rdi)`, `call rax` is `call *%rax`.) To read AT&T: source first, destination last, `$` marks a constant, `%` marks a register, and `disp(base,index,scale)` is the address.

---

## 2. Registers

### x86-64

| Register | Smaller views | Role in the System V convention |
|---|---|---|
| `rax` | `eax` `ax` `al` | return value; scratch |
| `rbx` | `ebx` `bx` `bl` | callee-saved |
| `rcx` | `ecx` `cx` `cl` | argument 4; variable shifts use `cl` |
| `rdx` | `edx` `dx` `dl` | argument 3; second half of wide results; `mul`/`div` use it |
| `rsi` | `esi` | argument 2 |
| `rdi` | `edi` | argument 1 |
| `rbp` | `ebp` | callee-saved; frame pointer in unoptimised code |
| `rsp` | `esp` | stack pointer |
| `r8` to `r15` | `r8d`, `r8w`, `r8b` ... | `r8`, `r9` arguments 5 and 6; `r10`, `r11` scratch; `r12` to `r15` callee-saved |
| `rip` | | instruction pointer; used in `[rip + g]` addressing |
| `rflags` | | the flags: `CF`, `ZF`, `SF`, `OF` and others |
| `xmm0` to `xmm15` | `ymm`, `zmm` | floating point and vectors |

Some instructions use registers **implicitly**: `mul` and `div` use `rax` and `rdx`; variable shifts need the count in `cl`; `rep movsb` uses `rsi`, `rdi` and `rcx`.

### AArch64

| Register | Role |
|---|---|
| `x0` to `x7` (`w0` to `w7` for the low 32 bits) | arguments and results |
| `x8` | address for a large returned struct |
| `x9` to `x15` | scratch |
| `x16`, `x17` | scratch used by the linker's call stubs |
| `x18` | platform register: reserved on Apple and Windows, so portable code does not touch it |
| `x19` to `x28` | callee-saved |
| `x29` | frame pointer |
| `x30` | **link register**: the return address after `bl` |
| `sp` | stack pointer; must be 16-byte aligned whenever it addresses memory |
| `xzr`, `wzr` | read as zero, writes discarded |
| `v0` to `v31` | floating point and vectors (`s0`, `d0` are the low 32 and 64 bits of `v0`) |
| `NZCV` | the flags: negative, zero, carry, overflow |

The program counter is not a general register; you reach it through branches and `adr`/`adrp`.

### What does a write to a smaller register do?

A 32-bit write **clears the upper half** of the 64-bit register, on both ISAs. x86-64 16-bit and 8-bit writes do not. Run on real hardware:

| Sequence | Value of the full 64-bit register |
|---|---|
| x86-64: `mov rax, -1` then `mov eax, 1` | `0x0000000000000001` |
| x86-64: `mov rax, -1` then `mov ax, 1` | `0xFFFFFFFFFFFF0001` |
| x86-64: `mov rax, -1` then `mov al, 1` | `0xFFFFFFFFFFFFFF01` |
| AArch64: `mov x0, #-1` then `mov w0, #1` | `0x0000000000000001` |

The 32-bit rule means the CPU never has to merge a new low half with an old high half (which would create a dependency on whatever wrote the register last). The 8- and 16-bit forms must merge, which is why compilers prefer a 32-bit operation or `movzx`.

A consequence: **the upper 32 bits of a register holding an `int` argument are undefined.** The caller may have left anything there. A callee that needs a 64-bit value must sign- or zero-extend it itself (`movsxd rax, edi`).

---

## 3. Flags and condition codes

### Where flags come from

Most arithmetic on x86 sets flags as a side effect. AArch64 sets them only when asked: `adds`, `subs`, `cmp`, `cmn`, `tst`, `ands`. The flags you need:

| | x86-64 | AArch64 |
|---|---|---|
| zero | `ZF` | `Z` |
| sign (top bit of the result) | `SF` | `N` |
| carry | `CF` | `C` |
| signed overflow | `OF` | `V` |

`cmp a, b` is a subtraction that **throws the result away** and keeps the flags (`cmn` is the same for addition, `test`/`tst` for AND). A later conditional instruction reads the flags. The same bits are both a signed and an unsigned number (tutorial 02), and the hardware can't know which you meant, so there are **two families of conditions** on the same flags:

| Question | x86 suffix (`jcc`, `setcc`, `cmovcc`) | AArch64 condition (`b.cond`, `cset`, `csel`) | Flags tested |
|---|---|---|---|
| equal | `e` | `eq` | Z = 1 |
| not equal | `ne` | `ne` | Z = 0 |
| signed `<` | `l` | `lt` | SF ≠ OF (N ≠ V) |
| signed `>=` | `ge` | `ge` | SF = OF |
| signed `>` | `g` | `gt` | ZF = 0 and SF = OF |
| signed `<=` | `le` | `le` | ZF = 1 or SF ≠ OF |
| unsigned `<` | `b` (below) | `lo` | x86: CF = 1; AArch64: C = 0 |
| unsigned `>=` | `ae` | `hs` | x86: CF = 0; AArch64: C = 1 |
| unsigned `>` | `a` | `hi` | x86: CF = 0 and ZF = 0; AArch64: C = 1 and Z = 0 |
| unsigned `<=` | `be` | `ls` | x86: CF = 1 or ZF = 1; AArch64: C = 0 or Z = 1 |
| signed overflow | `o` | `vs` | OF = 1 / V = 1 |
| negative | `s` | `mi` | SF = 1 / N = 1 |

**A trap hidden in the table.** After a subtraction, x86 sets `CF` when a **borrow** happened, while AArch64 sets `C` when **no** borrow happened. The mnemonics (`jb` and `b.lo`) hide the difference, but if you read raw flag values in a debugger the carry bit is opposite.

### Worked example: four comparisons on both ISAs

`cmp a, b` for 32-bit values, measured on real hardware:

| a | b | x86: CF ZF SF OF | `jl` (signed <) | `jb` (unsigned <) | AArch64: N Z C V |
|---|---|---|---|---|---|
| `0x80000000` | `1` | 0 0 0 1 | taken | not taken | 0 0 1 1 |
| `1` | `0x80000000` | 1 0 1 1 | not taken | taken | 1 0 0 1 |
| `5` | `5` | 0 1 0 0 | not taken | not taken | 0 1 1 0 |
| `3` | `5` | 1 0 1 0 | taken | taken | 1 0 0 0 |

Check the first row by hand. `0x80000000 - 1 = 0x7FFFFFFF`: the result is positive, so SF = 0. A negative number (top bit set) minus a positive one produced a positive result, which can't be right as a signed value, so OF = 1. There was no unsigned borrow (0x80000000 is bigger than 1), so CF = 0. Now `jl` asks SF ≠ OF, 0 ≠ 1, true: as signed, `0x80000000` is -2147483648, which is less than 1. And `jb` asks CF = 1: false, because as unsigned `0x80000000` is 2147483648, which is not below 1. **One `cmp`, two answers, and the jump you pick says which number system you mean.** This is why C's `unsigned` and `int` comparisons compile to different jumps from the same `cmp`.

Carry versus overflow on `add` (tutorial 02, now in hardware) are two different flags, `CF` for an unsigned wrap and `OF` for a signed overflow; Problem B has you tabulate them.

### From flags to values and selects

The compiler turns flags into data in three ways. Same C, both ISAs, `-O1`:

```c
int slt(int a, int b)           { return a < b; }        // unsigned version: setb / cset lo
int max2(int a, int b)          { return a > b ? a : b; }
int add_ovf(int a, int b)       { int r; return __builtin_add_overflow(a, b, &r); }
```

```text
x86-64                         AArch64
slt:    xor   eax, eax         slt:    cmp  w0, w1
        cmp   edi, esi                 cset w0, lt
        setl  al                       ret
        ret
max2:   mov   eax, esi         max2:   cmp  w0, w1
        cmp   edi, esi                 csel w0, w0, w1, gt
        cmovg eax, edi                 ret
        ret
add_ovf: xor  eax, eax         add_ovf: cmn w0, w1
        add   edi, esi                  cset w0, vs
        seto  al                        ret
        ret
```

- `setcc` / `cset` write 0 or 1 into a register from a flag condition (for `unsigned` arguments the compiler emits `setb` / `cset ... lo` instead).
- `cmovcc` / `csel` is a **conditional move**: both candidate values are computed, and the flag picks one. No branch means nothing to mispredict (tutorial 10), at the cost of always computing both.
- In `slt`, the `xor eax, eax` comes **before** the `cmp`. `xor` changes the flags, so zeroing after the compare would destroy it. `setl al` writes only the low byte, so the rest of `eax` must already be zero.

---

## 4. Calling conventions

Source files are compiled separately, libraries are built years before the programs that call them, and a C function may be called from assembly, Rust or Python. For that to work, every piece of code must agree, without seeing the other side, on where arguments go, where the result comes back, which registers a function may destroy and how the stack is kept. That agreement is the **calling convention**, part of the platform **ABI** (application binary interface). Break it and the failure isn't a compile error: it is a wrong value or a crash somewhere else.

| | System V AMD64 (Linux, BSD, x86-64 Unix) | AAPCS64 (Linux on ARM64) |
|---|---|---|
| Integer and pointer arguments | `rdi` `rsi` `rdx` `rcx` `r8` `r9` (6) | `x0` to `x7` (8) |
| Floating-point arguments | `xmm0` to `xmm7` | `v0` to `v7` |
| More arguments | on the stack, 8 bytes each; the first is at `[rsp+8]` when the callee starts | on the stack; the first is at `[sp]` when the callee starts |
| Return value | `rax` (`rdx` for a second 8 bytes); `xmm0` for floats | `x0` (`x1` for a second 8 bytes); `v0` for floats |
| Large struct return | caller passes a hidden pointer in `rdi`, callee returns it in `rax` | caller passes a hidden pointer in `x8` |
| **Callee-saved** (function must preserve) | `rbx` `rbp` `r12` to `r15` | `x19` to `x28`, `x29`, `x30` (before returning, it must again hold the return address), low 64 bits of `v8` to `v15` |
| **Caller-saved** (function may overwrite) | `rax` `rcx` `rdx` `rsi` `rdi` `r8` to `r11`, all `xmm`, flags | `x0` to `x17`, `v0` to `v7`, `v16` to `v31`, flags |
| Return address | pushed on the stack by `call` | in `x30`, written by `bl` |
| Stack alignment | `rsp` multiple of 16 at the `call` | `sp` always 16-byte aligned when used |
| Red zone | 128 bytes below `rsp` | none |

(A function's own **arguments** count as caller-saved too: the callee may reuse `rdi` freely.)

### Arguments that don't fit in registers

```c
long sum9(long a, long b, long c, long d, long e, long f, long g, long h, long i)
{ return a+b+c+d+e+f+g+h+i; }
```

```text
x86-64                                  AArch64
sum9:   add rsi, rdi                    sum9:   ldr x8, [sp]            <- 9th argument
        lea rax, [rdx + rcx]                    add x9, x1, x0
        add rax, rsi                            add x10, x2, x3
        add rax, r8                             add x11, x4, x5
        add rax, r9                             add x9, x9, x10
        add rax, qword ptr [rsp + 8]  <- 7th    add x10, x11, x6
        add rax, qword ptr [rsp + 16] <- 8th    add x8, x7, x8
        add rax, qword ptr [rsp + 24] <- 9th    add x9, x9, x10
        ret                                     add x0, x9, x8
                                                ret
```

x86-64 has six argument registers, so arguments 7 to 9 are on the stack, at `[rsp+8]` and up (`[rsp]` holds the return address). AArch64 has eight, so only the 9th spills, and it sits at `[sp]`. This is a real difference between ISAs: functions with 7 or 8 arguments are cheap on AArch64 and not on x86-64.

### The rule behind most compiler output

> **A value that must survive a call lives in a callee-saved register or on the stack.**

Every call may destroy all caller-saved registers, so a live value can't sit in one. Take a function that needs `a` after a second call:

```c
int ext(int);
int two(int x) { int a = ext(x); return a + ext(a); }
```

```text
x86-64                          AArch64
two:  push rbx                  two:  stp x29, x30, [sp, #-32]!
      call ext@PLT                    str x19, [sp, #16]
      mov  ebx, eax                   mov x29, sp
      mov  edi, eax                   bl  ext
      call ext@PLT                    mov w19, w0
      add  eax, ebx                   bl  ext
      pop  rbx                        add w0, w0, w19
      ret                             ldr x19, [sp, #16]
                                      ldp x29, x30, [sp], #32
                                      ret
```

Read the x86 side. The first call's result arrives in `eax`; the code keeps a copy in `ebx` (callee-saved, so the second call won't destroy it). It also moves the result into `edi` as the argument to the second call. After that call, `eax + ebx` is the answer. `ebx` belongs to the caller of `two`, so `two` saves it with `push rbx` and restores it with `pop rbx`: that is what "callee-saved" costs.

On AArch64 the second `bl` needs no move because the first result is already in `w0`, the argument register. The frame also saves `x29` and `x30`, because `bl` **overwrites `x30`** with its own return address; without saving it, `two` could not return. That is why an AArch64 leaf function (one that calls nothing) needs no frame at all: `x30` is never overwritten, so it simply ends with `ret`.

(`@PLT` on the x86 call means the target is reached through the procedure linkage table, section 8.)

### Returning structs

| Size of the returned struct | x86-64 | AArch64 |
|---|---|---|
| up to 8 bytes | `rax` | `x0` |
| 9 to 16 bytes | `rax` and `rdx` | `x0` and `x1` |
| over 16 bytes | memory: hidden pointer in `rdi`, also returned in `rax` | memory: hidden pointer in `x8` |

For `struct big { long v[4]; } make_big(long x)` (32 bytes) the compiler's listing writes the fields through the hidden pointer: `mov [rdi], rsi`, `mov [rdi+8], rcx` ... and `mov rax, rdi` on x86-64, `stp x0, x9, [x8]` ... on AArch64. Notice that the real first argument `x` has moved: it is in `rsi` on x86-64 (because `rdi` carries the pointer) but still in `x0` on AArch64 (because `x8` carries it).

The takeaway: **the signature in assembly is not the signature in C.** Adding a large struct return silently shifts the x86-64 arguments by one register.

### Other conventions

Variadic functions (`printf`) spill the argument registers into a save area, and on x86-64 the caller sets `al` to the number of vector registers used. Windows x64 uses a different convention (arguments in `rcx, rdx, r8, r9`, a 32-byte shadow space, 32-bit `long`); Apple arm64 follows AAPCS64 with changes (reserved `x18`, variadic arguments always on the stack). If an interviewer asks "what is the calling convention?", open with "on which platform?"; the two columns above are the Linux answer.

**System calls use a different register convention from function calls.** Linux x86-64: the syscall number goes in `rax`, the arguments in `rdi, rsi, rdx, r10, r8, r9` (`r10` takes the place of `rcx`, because the `syscall` instruction overwrites `rcx` and `r11`), and the result comes back in `rax`, where a small negative value means an error (-errno). Linux AArch64: `svc #0` with the number in `x8`, the arguments in `x0` to `x5`, and the result in `x0`. I checked the x86-64 version with inline `syscall`: `write(1, msg, 3)` returned 3, and `write(-1, msg, 3)` returned -9 (-EBADF). The libc wrappers (`write`, `read`) turn a negative result into `-1` and set `errno`. Tutorial 26 covers what the hardware does on the way into the kernel.

---

## 5. The stack and its frames

The stack is a region of memory, one per thread, used last-in-first-out, that **grows toward lower addresses**. A function's **frame** is the block of stack that belongs to one active call. It may hold: the return address (x86-64 only), saved callee-saved registers, local variables that don't fit in registers, spill slots, and stack arguments for calls it makes.

x86-64 has `push` and `pop`, and `call`/`ret` use the stack implicitly. AArch64 has none of those: a function moves `sp` explicitly (`sub sp, sp, #32`, or the pre-index store `stp x29, x30, [sp, #-32]!`) and stores with ordinary `str`/`stp`.

### Two frames, drawn

The frame of `two` above at `-O1`, with the call to `ext` in progress (higher addresses are at the top of the picture):

```text
 x86-64 (16 bytes)                       AArch64 (32 bytes)

 higher addresses                        higher addresses
 +--------------------------+            +--------------------------+
 | return address (by call) |            | (caller's frame)         |
 +--------------------------+            +--------------------------+
 | saved rbx                |            | padding                  |  [sp+24]
 +--------------------------+ <- rsp     | saved x19                |  [sp+16]
 lower addresses                         | saved x30 (return addr)  |  [sp+8]
                                         | saved x29 (frame ptr)    |  [sp+0]  <- sp, x29
                                         +--------------------------+
                                         lower addresses
```

The same function at **`-O0`** keeps every variable in memory and builds a frame pointer chain. Its prologue and epilogue, which you will see at the start and end of unoptimised functions:

```text
x86-64                                       AArch64
two:  push rbp               ; save caller's   two:  sub  sp, sp, #32
      mov  rbp, rsp          ; frame pointer         stp  x29, x30, [sp, #16]    ; frame record
      sub  rsp, 16           ; room for locals       add  x29, sp, #16
      ...                    ; locals at [rbp-4]..   ...                         ; locals at [sp+4]..
      add  rsp, 16                                   ldp  x29, x30, [sp, #16]
      pop  rbp                                       add  sp, sp, #32
      ret                                            ret
```

The x86 prologue `push rbp; mov rbp, rsp` saves the caller's frame pointer and points `rbp` at it, so `[rbp-4]` and `[rbp-8]` are locals and `[rbp+8]` is the return address; the epilogue `pop rbp; ret` undoes it. A debugger or profiler walks the stack by following `rbp -> [rbp] -> [[rbp]]`. Optimised code usually frees `rbp` for ordinary use and relies on separate unwind tables instead, so tools without those tables can't walk its stack. AArch64 builds the same chain with `x29`: `stp x29, x30` stores the caller's frame pointer and the return address together as a two-word **frame record**.

The cost of `-O0`: `two` is 17 instructions on x86-64 and 16 on AArch64, loading and storing memory around every use. At `-O1` the same job needs no variable in memory and takes 8 and 10 (section 6).

### Alignment

The System V rule: **at the moment a `call` executes, `rsp` is a multiple of 16.** The `call` pushes 8 bytes, so a function starts with `rsp ≡ 8 (mod 16)`. Every `push` subtracts 8 and flips that residue. So for a function that makes calls:

> bytes pushed or subtracted after entry ≡ 8 (mod 16)

`two` at `-O1` does one `push rbx`: 8 bytes, correct. At `-O0` it does `push rbp` and `sub rsp, 16`: 24 ≡ 8, correct. A function that pushed `rbp` and `rbx` (16 bytes) would also have to subtract 8, 24, or 40 before calling.

Why care? Compilers and libraries assume it. SSE instructions such as `movaps` fault on a misaligned address, and compiled code uses them to copy stack objects of 16 bytes or more. A misaligned stack often goes unnoticed for years and then crashes inside `printf` or `memcpy` when someone adds a `double` argument. AArch64 is stricter: `sp` must be 16-byte aligned whenever it addresses memory, which is why its prologues subtract 16, 32, 48, never 8 (hence the padding word in the diagram).

### The red zone

System V promises that the **128 bytes below `rsp`** won't be touched by signal handlers, so a **leaf** function can keep its locals there without adjusting `rsp` at all:

```c
int leaf(int x) { volatile int t = x; return t + 1; }     // volatile forces a memory round trip
```

```text
x86-64                                  AArch64
leaf: mov [rsp - 4], edi                leaf: sub sp, sp, #16
      mov eax, [rsp - 4]                      str w0, [sp, #12]
      inc eax                                 ldr w8, [sp, #12]
      ret                                     add w0, w8, #1
                                              add sp, sp, #16
                                              ret
```

x86-64 stores below `rsp` with no `sub`; AArch64 has no red zone and must move `sp` first. **Kernel code on x86-64 is compiled with `-mno-red-zone`**: an interrupt can arrive at any instruction, and the CPU pushes its state onto the kernel stack right at `rsp`, which would overwrite whatever a leaf function parked below it.

**Stack protector.** With `-fstack-protector-strong` (the default on several distributions), a function with a char array or an address-taken local gets a random **canary** between its locals and the saved return address, checked before `ret`; an overflow that reaches the return address must first change the canary, and the program aborts instead of returning to attacker-chosen code.

---

## 6. Reading compiler output

A method that works on any function:

1. **Find the arguments** by the convention: `edi, esi, edx` or `w0, w1, w2` are the first three `int` arguments.
2. **Find the result**: `eax` or `w0` just before `ret`.
3. **Find loops**: a branch back to an earlier label.
4. **Find memory accesses** and read their addressing modes (tutorial 06): `[rdi + 4*rdx]` is `a[i]` for `int *a`.
5. **Find calls** and check which registers carry the arguments.
6. **Map it back to the C**, instead of translating line by line.

### Optimisation level changes everything

The flags to know: `-O0` (no optimisation, easiest to debug), `-O1`/`-O2` (the normal range; `-O2` is the usual release level), `-O3` (more aggressive inlining, unrolling and vectorisation, at the cost of code size), `-Os` (optimise for size) and `-Ofast` (`-O3` plus `-ffast-math`, which lets the compiler reorder floating-point arithmetic). `-g` adds debug information (DWARF) without changing the generated code, and `strip` removes the symbols afterwards.

`int add3(int a, int b, int c) { return a + b + c; }`:

```text
x86-64 -O0                           x86-64 -O1
add3: push rbp                       add3: lea eax, [rdi + rsi]
      mov  rbp, rsp                        add eax, edx
      mov  [rbp - 4], edi                  ret
      mov  [rbp - 8], esi
      mov  [rbp - 12], edx             AArch64 -O1
      mov  eax, [rbp - 4]              add3: add w8, w1, w0
      add  eax, [rbp - 8]                    add w0, w8, w2
      add  eax, [rbp - 12]                   ret
      pop  rbp
      ret
```

At `-O0` every argument is stored to the stack and loaded back (10 instructions, the code a debugger wants, since every variable has a home in memory). At `-O1` the values never leave registers (3 instructions), and `lea` does the first add. Use `-O0` to learn the structure, and `-O1` or `-O2` to see what the shipped code does.

For `int sum(int *a, int n)` (the loop from tutorial 06), instruction counts for the whole function:

| | `-O0` | `-O1` | `-O2` |
|---|---|---|---|
| x86-64 | 21 | 13 (one is a padding `nop`) | 41 |
| AArch64 | 25 | 12 | 35 |

`-O0` keeps `s` and `i` in memory. `-O1` gets the 4-instruction loop of tutorial 06. At `-O2`, clang **vectorises and unrolls** the loop (tutorial 21): the code grows to about 40 instructions, but it processes several elements per iteration. More instructions are not slower code, and fewer are not faster: look at the loop body and at memory traffic.

### Things the compiler does that look like magic

**Division by a constant becomes a multiply.** Division is slow, so for `x / 10` the compiler multiplies by a precomputed reciprocal and shifts:

```text
unsigned udiv10(unsigned x) { return x / 10; }

x86-64                          AArch64
udiv10: mov  ecx, edi          udiv10: mov  w8, #52429
        mov  eax, 3435973837           movk w8, #52428, lsl #16
        imul rax, rcx                  umull x8, w0, w8
        shr  rax, 35                   lsr  x0, x8, #35
        ret                            ret
```

`3435973837 = 0xCCCCCCCD = ceil(2^35 / 10)`, so `x * 0xCCCCCCCD >> 35` is `x / 10` for every 32-bit `x` (I checked 2.2 million values including both ends of the range). `mov ecx, edi` zero-extends `x` into `rcx`, and the 64-bit multiply is what stops the product overflowing.

**Branches become arithmetic.**

```text
int clamp0(int x) { return x < 0 ? 0 : x; }

x86-64                       AArch64
clamp0: xor   eax, eax       clamp0: bic w0, w0, w0, asr #31
        test  edi, edi               ret
        cmovg eax, edi
        ret
```

`asr #31` turns `x` into all ones if negative and zero otherwise; `bic` (bit clear) then zeroes `x` when the mask is all ones. No branch, no compare.

**Tail calls become jumps.** When a function ends with `return g(x + 1);`, the compiler at `-O2` does not `call` and then `ret`; it sets up the argument and **jumps** to `g`, so `g`'s own `ret` returns straight to the original caller and no extra frame is used. `int f(int x) { return g(x + 1); }` becomes `add edi, 1` then `jmp g` on x86-64 (`add w0, w0, #1` then `b g` on AArch64). I checked both with gcc 13 and clang 18. This is why a backtrace can be missing a frame in optimised code.

**More of the same.** The earlier `x * 5` became `lea eax, [rdi + 4*rdi]` (tutorial 06); a loop that counts negative numbers becomes a shift of the sign bit added straight into the counter; and a string-length loop starts its counter at -1 so that the test and the increment fit together. Problem A has the last two to decode.

---

## 7. Inline assembly

Sometimes you want one instruction inside a C function: read a counter, an atomic the compiler doesn't expose, a barrier. GCC and Clang's **extended inline assembly** lets you write the instruction and *describe* it to the compiler, which then picks the registers:

```c
int asm_add(int a, int b) {
#if defined(__x86_64__)
    __asm__("add %1, %0" : "+r"(a) : "r"(b) : "cc");           // AT&T order: add src, dst
#elif defined(__aarch64__)
    __asm__("add %w0, %w0, %w1" : "+r"(a) : "r"(b));
#endif
    return a;
}
```

The template is followed by up to three colon-separated lists: **outputs**, **inputs** and **clobbers**. In the template `%0`, `%1` name the operands in order (outputs first); on AArch64 `%w0` is the 32-bit view of operand 0.

| Constraint | Meaning |
|---|---|
| `"r"(x)` | operand is read, from any general register |
| `"=r"(x)` | operand is written (output only) |
| `"+r"(x)` | operand is read and written |
| `"m"(x)` | a memory operand |
| `"i"(n)` | a compile-time constant |
| `"cc"` clobber | the template changes the flags |
| `"memory"` clobber | the template may read or write memory the compiler can't see |
| `asm volatile` | don't delete the statement, don't reorder it against other volatile operations |

The compiler's output for the x86 version (gcc 13, default AT&T syntax): `movl %edi, %eax`, then your `add %esi, %eax`, then `ret`. It chose `eax` for `a` and `esi` for `b` by itself, which is exactly the point.

**The trap: the template uses whatever syntax the compiler is writing.** Compile the same source with `-masm=intel` and `add %1, %0` is now read as Intel order (destination first), the compiler emits `add esi, eax`, and the function **returns 40 instead of 42** for `asm_add(40, 2)`. I ran both. If your inline asm must survive both settings, use dialect alternatives (`"add {%1, %0|%0, %1}"`, which gave 42 in both modes when I tried it) or keep one syntax per file.

The most common inline-asm bug is an incomplete description: if the template changes something you didn't list (a register, the flags, memory), the compiler keeps using the old value and the code breaks at `-O2` only.

```c
void compiler_barrier(void) { __asm__ volatile("" ::: "memory"); }
```

This emits **no instruction**, yet it matters: the `"memory"` clobber says "anything in memory may have changed", so the compiler may not move loads or stores across it. It does **not** stop the CPU from reordering; that needs a fence instruction (tutorial 20).

**When to use assembly at all.** For one operation, prefer a builtin (`__builtin_clz`, `__builtin_popcount`, `__builtin_bswap32`, `__builtin_add_overflow`); for SIMD, intrinsics (tutorial 21); for atomics, `std::atomic` or `<stdatomic.h>` (tutorial 20). Inline assembly is for one special instruction (`rdtsc`, `isb`, `mrs`), and a `.S` file for entry points, context switches and boot code. Assembly gains nothing in speed over a good compiler for ordinary code and costs portability and readability.

---

## 8. Assembler, linker and loader

(The C track goes deeper; this is the part you need for architecture interviews.) A program goes through four tools:

```text
 f.c ---> compiler ---> f.s ---> assembler ---> f.o  \
 g.c ---> compiler ---> g.s ---> assembler ---> g.o   +--> linker ---> a.out --loader--> process
                                  libraries (.a, .so) /
```

- The **compiler** turns C into assembly text.
- The **assembler** turns assembly into machine code in an **object file** (`.o`). The code isn't final: addresses of other functions and globals are not known yet.
- The **linker** combines object files and libraries, decides where everything lives, and patches the unknown addresses (relocation).
- The **loader** (the kernel plus the dynamic linker) maps the file into memory and starts it.

### What is in an object file

On Linux, object files, executables and libraries are all **ELF** files. The parts that matter:

| Section | Contents |
|---|---|
| `.text` | machine code |
| `.rodata` | constants such as string literals |
| `.data` | initialised global and static variables |
| `.bss` | zero-initialised globals (takes no space in the file, only a size) |
| `.symtab`, `.strtab` | the symbol table: names of functions and globals, and where they are |
| `.rela.text` | **relocations**: places in the code the linker must patch |

Two source files:

```c
/* helper.c */                             /* main.c */
int counter = 5;                           #include <stdio.h>
int helper(int x) { return x + counter; }  int helper(int);
                                           int main(void) { puts("hi"); return helper(10); }
```

`nm` lists symbols: `T` is code defined here, `D` is initialised data defined here, `r` is read-only data (the string), and `U` is **undefined**, meaning someone else must provide it.

```text
$ nm main.o                        $ nm helper.o
0000000000000000 r .LC0            0000000000000000 D counter
                 U helper          0000000000000000 T helper
0000000000000000 T main
                 U puts
```

### Relocation: the linker's job

Disassemble `main.o` with relocations shown (`objdump -dr`):

```text
  15:  e8 00 00 00 00     call   1a <main+0x1a>
          16: R_X86_64_PLT32   helper-0x4
```

The compiler didn't know where `helper` would end up, so it emitted a `call` (`e8`) with a displacement of **zero** and left a note: "at offset 0x16, put the address of `helper`, minus 4". After linking, the same instruction in the executable reads:

```text
  114e:  e8 05 00 00 00   call   1158 <helper>
```

The displacement is now 5. A `call`'s displacement counts from the **end of the instruction** (0x114e + 5 = 0x1153), so 0x1153 + 5 = 0x1158, the address of `helper`. That "minus 4" in the note is the same bookkeeping: the 4-byte field ends 4 bytes after where it starts. The data access works the same way: `add eax, [rip+0x0]` in `helper.o` with a relocation `R_X86_64_PC32 counter-0x4` becomes `[rip+0x2eb0]  # 4010 <counter>` after linking (PC-relative addressing, tutorial 06).

**A relocation is a to-do note from the assembler to the linker.** That is the whole idea. The linker also resolves `U` symbols against other objects and libraries, and reports "undefined reference" when nothing defines one.

### Static versus dynamic linking

| | Static (`gcc -static`) | Dynamic (default) |
|---|---|---|
| Library code | copied into the executable | stays in a shared library (`libc.so.6`) |
| Size of our example | **785,480 bytes** | **15,992 bytes** |
| Startup | fast, nothing to find | the loader must find and map libraries |
| Updates | rebuild to pick up a library fix | fix once, every program benefits |

A dynamically linked program calls library functions through the **PLT** (procedure linkage table) and the **GOT** (global offset table), because a shared library's address is only known at run time. In our example `main` calls `puts@plt`, a tiny stub whose first instruction is `jmp QWORD PTR [rip+0x2f9a]`: an indirect jump through a GOT slot that the loader fills with the real address of `puts` (`readelf -r` shows it as `R_X86_64_JUMP_SLOT puts`). With **lazy binding** the slot first points back into the loader, which resolves the symbol on the first call; this build uses `BIND_NOW`, so everything is resolved at startup. `ldd app` lists the libraries a program needs, and `readelf -l app` shows the program interpreter (`/lib64/ld-linux-x86-64.so.2`), which is the dynamic linker.

### What the loader does

1. `execve` asks the kernel to run the file. The kernel reads the **ELF header** and maps the segments (code, data) into a new address space (the virtual memory of tutorial 15).
2. If the file has an interpreter, the kernel starts it first. It maps the shared libraries, performs the relocations that must happen at run time, and jumps to the program.
3. Execution begins at the ELF **entry point**, which is `_start` (0x1050 in our file), not `main`. `_start` calls `__libc_start_main`, which sets up the C runtime and then calls `main`. When `main` returns, its value goes to `exit`.

An executable built as a **PIE** (position-independent executable, the default on current Linux) is loaded at a random base address each run (ASLR), which is why its code uses `[rip + g]` rather than absolute addresses (tutorial 06). On macOS the same ideas apply with different names: Mach-O files, `dyld` as the dynamic linker. Thread-local variables are reached through a dedicated register (`fs` on x86-64 Linux, `TPIDR_EL0` on AArch64). Weak symbols, `LD_PRELOAD` interposition, `dlopen` and symbol visibility are covered in the C track (tutorial 06).

---

## 9. Mistakes that cost points

1. **Using the wrong jump for signedness.** After `cmp`, `jl`/`jg` mean signed, `jb`/`ja` mean unsigned. Using `jl` on pointers or `unsigned` values gives wrong results above 2^31.
2. **Assuming the upper bits of a 32-bit argument are zero.** They are undefined. Extend explicitly.
3. **Clobbering flags between a compare and its jump**, or putting `xor reg, reg` after the `cmp`.
4. **Forgetting that `call` overwrites `x30`** on AArch64 (a non-leaf function must save it), and that callee-saved registers must be restored on every exit path.
5. **Misaligning the stack before a call.** Count pushes plus subtracted bytes: they must be ≡ 8 (mod 16) after entry on x86-64.
6. **Treating assembly signatures as C signatures.** A large struct return adds a hidden pointer argument and shifts the others.
7. **Incomplete inline asm descriptions.** Forgetting a clobber works at `-O0` and fails at `-O2`. Writing the template in the wrong syntax (AT&T vs Intel operand order) gives the wrong answer silently.

---

## 10. Questions you will be asked

**Describe the System V x86-64 calling convention.** Integer arguments in `rdi, rsi, rdx, rcx, r8, r9`, floating point in `xmm0` to `xmm7`, more on the stack. The result is in `rax` (or `xmm0`). `rbx, rbp, r12` to `r15` are callee-saved; everything else may be overwritten. The stack is 16-byte aligned at the call, and the caller's `call` pushes the return address.

**What is the difference between caller-saved and callee-saved registers?** A callee-saved register must hold the same value when the function returns, so a function that uses it saves and restores it. A caller-saved register may be destroyed by any call, so the caller saves it if it needs the value afterwards. Values that live across a call go in callee-saved registers; short-lived temporaries in caller-saved ones.

**What happens on a function call and return?** On x86-64, `call` pushes the return address and jumps; the callee may push `rbp`, set up a frame and allocate locals; `ret` pops the return address into `rip`. On AArch64, `bl` puts the return address in `x30`; a non-leaf function saves it in its frame and `ret` jumps to `x30`.

**What is the red zone, and why does the kernel not use it?** A 128-byte area below `rsp` that user-space leaf functions can use without moving `rsp`. The kernel can't, because an interrupt pushes state at `rsp` at any instruction and would overwrite it.

**Why must the stack be aligned?** Compiled code and libraries assume 16-byte alignment and use SSE instructions such as `movaps` that fault on misaligned addresses; AArch64 faults on a misaligned `sp` directly.

**`jl` versus `jb`?** Both follow a `cmp`. `jl` tests SF ≠ OF (signed less), `jb` tests CF (unsigned below). The same bits can compare differently under the two views.

**What do an assembler, a linker and a loader each do?** The assembler produces machine code with placeholders called relocations (notes to patch an address once a symbol's final location is known); the linker combines objects and libraries and fills the placeholders; the loader maps the executable and libraries into memory and starts execution at the entry point.

**Static versus dynamic linking?** Static copies library code into the executable: larger, self-contained, no startup lookup. Dynamic shares one library copy among programs and allows updates without rebuilding, but needs the loader, a PLT/GOT indirection on calls, and the right library version at run time.

**What are the PLT and GOT?** The GOT is a table of addresses the loader fills with the real locations of shared-library symbols. The PLT is a set of small stubs that jump through the GOT, so a call site can be compiled before the address is known.

**Why does a 32-bit write to `eax` clear the top half of `rax`?** So the CPU never has to merge a new low half with the old upper bits, which would create a dependency on the previous writer.

---

## 11. Problems

**How to use this section.** Try each problem before looking at the answers at the bottom. A, B and C are paper problems (about 25 minutes each); D uses a compiler and a Linux machine (about 35 minutes).

### Problem A (paper): read compiled code

Here are two functions, one for each ISA. Both were compiled with `-O1`.

```text
Listing 1 (x86-64, Intel)               Listing 2 (AArch64)
f:  mov  rax, -1                        g:  cmp  w1, #1
.L: cmp  byte ptr [rdi + rax + 1], 0        b.lt .Lend
    lea  rax, [rax + 1]                     mov  x8, x0
    jne  .L                                 mov  w0, wzr
    ret                                     mov  w9, w1
                                        .L: ldr  w10, [x8], #4
                                            subs x9, x9, #1
                                            add  w0, w0, w10, lsr #31
                                            b.ne .L
                                            ret
                                        .Lend: mov w0, wzr
                                            ret
```

(a) Write the C function behind each listing (hint: the arguments are one pointer, or a pointer and an `int`; the result is an integer).
(b) In Listing 1, why does the loop start with `rax = -1` and compare `[rdi + rax + 1]`? Why is the increment done with `lea` instead of `add`?
(c) In Listing 2, where is the test of the element? Which instruction handles the "is it negative" part, and why is there no branch for it?
(d) In Listing 2, name the instruction that updates the loop counter and also sets the flags, and the instruction that uses a post-index addressing mode.

*Where:* paper; compile your own guesses on godbolt.org with clang `-O1` (x86-64 and ARM64) to check. *How:* method in section 6: arguments, result, loop, memory accesses. For (c), think about what the top bit of a negative `int` is.

### Problem B (paper): flags by hand

(a) Compute the x86-64 flags CF, ZF, SF, OF after `cmp eax, ebx` with `eax = 0x7FFFFFFF` and `ebx = 0xFFFFFFFF`. Do `jl` and `jb` jump? What does each answer mean in C terms? Give N, Z, C, V for AArch64 as well.
(b) Fill in CF and OF for the additions `0x7FFFFFFF + 1`, `0xFFFFFFFF + 1`, `0x80000000 + 0x80000000`.
(c) Which condition do you use for `if (a >= b)` when `a` and `b` are `int`, and when they are `unsigned`? Give x86 and AArch64 names.

*Where:* paper; check by running a tiny inline-asm program (section 7 shows the pattern) if you want. *How:* do the subtraction as binary, then read each flag off the definition: SF = top bit of the result, CF/borrow = unsigned `a < b`, OF = signed result doesn't fit.

### Problem C (paper): stack frames and a function in assembly

(a) An x86-64 function does `push rbp`, `push rbx`, then `sub rsp, N`, then calls another function. Which values of N keep the stack aligned?
(b) A C function takes nine `long` arguments. Where is the 7th argument when the x86-64 callee starts executing? Where is the 9th argument on AArch64?
(c) Write, in assembly for both x86-64 and AArch64, the functions `void swap(int *a, int *b)` and `int clamp0(int x)` (returns `x` if positive, otherwise 0). Make `clamp0` branch-free.

*Where:* paper; compare with the compiler (clang `-O1`) on godbolt.org. *How:* (a) at entry `rsp ≡ 8 (mod 16)`; (b) section 4's table; (c) pointers in `rdi, rsi` / `x0, x1`, a load and a store each; `clamp0` needs `cmov` / `csel`, or a trick like `bic`.

### Problem D (hands-on): optimisation levels and a link

Part 1. Compile `sum(int *a, int n)` from tutorial 06 at `-O0`, `-O1` and `-O2` for x86-64 and AArch64 and count the instructions in the function. What happens at `-O2`, and why isn't more code worse?

Part 2. On a Linux machine (or Docker), build the two-file example of section 8 with `gcc -O1 -c main.c helper.c`, then run `nm main.o`, `objdump -dr main.o`, link with `gcc -o app main.o helper.o`, and `objdump -d app`. Find the relocation before linking and the resolved `call` after. Then build with `-static` and compare file sizes.

*Where:* part 1: godbolt.org or `clang -O1 -S -o - f.c`. Part 2: any Linux shell; on a Mac use a Linux container, since macOS produces Mach-O, not ELF. *How:* part 1, count lines of the function body; part 2, follow section 8.

---

## Answers

Try before reading.

### Problem A

(a) Listing 1 is `strlen`: `long f(const char *s) { long n = 0; while (s[n]) n++; return n; }`. Listing 2 counts negative elements: `int g(const int *a, int n) { int c = 0; for (int i = 0; i < n; i++) if (a[i] < 0) c++; return c; }`.

(b) Each pass tests byte `s[rax+1]` and then does `rax = rax + 1`, so after the pass that tests `s[k]`, `rax` equals `k`. The pass that finds the NUL at index `k` therefore leaves `rax = k`, which is the length. Starting at -1 makes the first pass test `s[0]`. The increment uses `lea` because **`lea` does not change the flags**: the flags from `cmp` must survive until `jne`, and an `add` in between would replace them with the flags of the add.

(c) The test is `add w0, w0, w10, lsr #31`. `lsr #31` extracts the sign bit of the element: 1 if negative, 0 otherwise, so adding it to the counter *is* the "if negative, count it" with no branch. The flag-setting `b.lt` and `b.ne` are only for the loop itself.

(d) `subs x9, x9, #1` decrements the counter and sets the flags that `b.ne` reads. `ldr w10, [x8], #4` is the post-index form: load from `x8`, then add 4 to `x8`.

### Problem B

(a) `0x7FFFFFFF - 0xFFFFFFFF = 0x80000000` (mod 2^32). SF = 1 (top bit of the result), ZF = 0, CF = 1 (unsigned 2147483647 < 4294967295, so a borrow), OF = 1 (signed 2147483647 - (-1) = 2147483648 doesn't fit). `jl` tests SF ≠ OF: 1 ≠ 1 is false, **not taken** (signed: 2147483647 < -1 is false). `jb` tests CF = 1: **taken** (unsigned: 2147483647 < 4294967295 is true). On AArch64: N = 1, Z = 0, **C = 0** (a borrow occurred, so "no borrow" is false), V = 1; `lt` (N ≠ V) false, `lo` (C = 0) true. I measured these on real hardware.

(b) `0x7FFFFFFF + 1`: CF = 0, OF = 1. `0xFFFFFFFF + 1`: CF = 1, OF = 0. `0x80000000 + 0x80000000`: CF = 1, OF = 1.

(c) Signed: x86 `jge` (or `setge`, `cmovge`), AArch64 `ge`. Unsigned: x86 `jae`, AArch64 `hs`.

### Problem C

(a) At entry `rsp ≡ 8 (mod 16)`. After `push rbp` and `push rbx` that is 16 bytes, which leaves `rsp ≡ 8`. The call needs `rsp ≡ 0`, so `sub rsp, N` must add another 8 mod 16: **N = 8, 24, 40, ...** (N ≡ 8 mod 16).

(b) x86-64: the 7th argument is at `[rsp + 8]` (the return address is at `[rsp]`), the 8th at `[rsp + 16]`, the 9th at `[rsp + 24]`. AArch64: the first eight arguments are in `x0` to `x7`, so the 9th is the only stack argument, at `[sp]`.

(c) The compiler's `-O1` versions (yours may differ, but a load and a store per pointer is the shape):

```text
x86-64                          AArch64
swap:   mov eax, [rdi]          swap:   ldr w8, [x1]
        mov ecx, [rsi]                  ldr w9, [x0]
        mov [rdi], ecx                  str w8, [x0]
        mov [rsi], eax                  str w9, [x1]
        ret                             ret

clamp0: xor   eax, eax          clamp0: bic w0, w0, w0, asr #31
        test  edi, edi                  ret
        cmovg eax, edi
        ret
```

`swap` needs two temporaries because both loads must happen before either store. A conditional-move version of `clamp0` on AArch64 would be `cmp w0, #0; csel w0, w0, wzr, gt` (`wzr` supplies the zero); the compiler found the shorter `bic` trick.

### Problem D

Part 1. My counts (clang 18, whole function):

| | `-O0` | `-O1` | `-O2` |
|---|---|---|---|
| x86-64 | 21 | 13 (includes one alignment `nop`) | 41 |
| AArch64 | 25 | 12 | 35 |

`-O0` keeps `s` and `i` in memory; `-O1` makes the compact 4-instruction loop; `-O2` **vectorises and unrolls** the loop with SIMD, so it processes several elements per iteration and handles leftover elements separately. Longer code, shorter run time on large arrays. Your counts may differ by a few.

Part 2. Expected: `nm main.o` shows `U helper`, `T main`, `U puts`; `nm helper.o` shows `D counter`, `T helper`. `objdump -dr main.o` shows `e8 00 00 00 00 call` with `R_X86_64_PLT32 helper-0x4` right after it. After linking, the same call is `e8 05 00 00 00 call <helper>` (the displacement counts from the end of the instruction). `puts` is called through `puts@plt`. The dynamic executable is about 16 KB; the `-static` one is about 785 KB. Running `./app` prints `hi` and exits with status 15 (`helper(10)` returns 10 + `counter`, which is 5).

---

## Cheat sheet

- **Syntax:** Intel is `dest, src`; AT&T is `src, dest` with `%` and `$`. AArch64: `add x0, x1, x2` is `x0 = x1 + x2`; `str` writes the first register to memory.
- **Partial writes:** a 32-bit write zeroes the upper half; 8- and 16-bit x86 writes keep it. The top 32 bits of an `int` argument are undefined.
- **Flags:** `cmp a, b` = `a - b` discarded. Signed: `l ge g le` / `lt ge gt le`. Unsigned: `b ae a be` / `lo hs hi ls`. x86 `CF` = borrow, AArch64 `C` = no borrow.
- **Flags to values:** `setcc`/`cset` give 0 or 1; `cmovcc`/`csel` select. `xor reg, reg` before the `cmp`, not after.
- **System V x86-64:** args `rdi rsi rdx rcx r8 r9`, result `rax`; callee-saved `rbx rbp r12-r15`; `call` pushes the return address; stack args from `[rsp+8]`; `rsp` ≡ 0 (mod 16) at the call, ≡ 8 at entry; red zone 128 bytes.
- **AAPCS64:** args `x0-x7`, result `x0`, struct pointer `x8`; callee-saved `x19-x28`, `x29`, `x30`; return address in `x30` (save it in a non-leaf function); `sp` 16-byte aligned; no red zone.
- **Survives a call → callee-saved register or stack.**
- **Frames:** x86 `push rbp; mov rbp, rsp ... pop rbp; ret`; AArch64 `stp x29, x30, [sp, #-N]! ... ldp x29, x30, [sp], #N; ret`.
- **Reading output:** arguments by convention, result before `ret`, backward branch = loop, addressing modes = array/struct accesses. `-O0` all in memory; `-O1` clean; `-O2` unrolled and vectorised.
- **Inline asm:** outputs, inputs, clobbers (`"cc"`, `"memory"`); the template uses the compiler's current syntax; a `"memory"` clobber stops the compiler, not the CPU.
- **Toolchain:** assembler makes `.o` with relocations; linker resolves symbols and patches them; loader maps the file, runs the dynamic linker, enters `_start`. PLT stubs jump through the GOT. Static: self-contained and big; dynamic: shared and small.
- **Tools:** `clang -S`, `objdump -dr`, `nm`, `readelf -S -r -d`, `ldd`, godbolt.

**Next:** tutorial 08, Datapath and Control Unit (the single-cycle and multi-cycle datapath, hardwired and microprogrammed control, and how one instruction moves through the hardware). Say "next" when you are ready.
