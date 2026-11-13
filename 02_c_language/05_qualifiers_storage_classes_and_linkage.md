# C 05: Qualifiers, Storage Classes and Linkage

*Part of the [C language plan](00_plan.md) · Time: about 4 hours (2.5 reading, 1.5 problems) · You need: day-to-day C, a compiler (`gcc` or `clang`), [C 01](01_types_conversions_and_undefined_behavior.md) for undefined behaviour and [C 03](03_memory_stack_heap_and_malloc.md) for where objects live. There is no companion demo file: every program below was compiled and run, and its real output is printed under it*

Four small keywords (`const`, `volatile`, `restrict`, `static`) plus `extern` and `inline` carry a large share of the C interview questions and of the bugs in production C. They look like decoration, and each one is a contract with the compiler or the linker: `const` and `restrict` let the compiler assume things, `volatile` forbids it to assume anything, `static` and `extern` decide which names the linker sees, and `inline` has different rules in C than in C++. The mistakes are of one kind: asking a keyword to do a job it was not given. `volatile` is not thread safety, `const` is not "never changes", `inline` is not "always inlined", and `restrict` is a promise you can break without any diagnostic. This tutorial states what each one guarantees, shows the compiled assembly or the real linker output, measures what it costs, and ends with the kernel idioms (`READ_ONCE`, `WRITE_ONCE` and the compiler barrier) that replace `volatile` where `volatile` is the wrong tool.

By the end you can:

- read a declaration with `const` (`const int *`, `int *const`, `const int *const`) and say which assignments compile and why `const char **` is not `char **`;
- say what writing to a `const` object does (read-only memory, or a result that changes with the optimisation level) and why `strchr` is a hole in `const` correctness;
- say what `volatile` guarantees and what it does not (atomicity, ordering between threads), with a program that hangs without it and a counter that loses updates with it;
- write `READ_ONCE`, `WRITE_ONCE` and a compiler barrier, and say how a compiler barrier differs from a CPU barrier;
- explain `restrict`, show what happens when the promise is broken (measured), and say when it helps;
- explain internal and external linkage, `extern`, declarations against definitions, tentative definitions and why `int x;` in a header breaks the link, and read the letters of `nm` output;
- explain C99 `inline` (it differs from C++), why `static inline` is the safe form for headers, and what an out-of-line call costs;
- name the `static` buffer trap, and fix it.

**Where the numbers come from.** Every program was compiled with GCC 13.3 (`-std=c11 -Wall -Wextra`) on an x86-64 Linux container (glibc 2.39) and run; the outputs and the linker messages are what the tools printed (temporary object names are replaced by `OBJ`). The helper code printed in the tutorial (a const-correct number parser and field splitter, the `restrict` kernels, and the `READ_ONCE` macros) is the real source, tested against oracles: 399,683 checks passed under AddressSanitizer and UBSan. The parser is compared with `strtoull` on random strings; the kernels with plain models, including exact overlap for the version without `restrict`. I tried 16 deliberate bugs in it (tabs not skipped, an off-by-one at the 32-bit limit, a field separator that matches a letter, a `WRITE_ONCE` that stores the wrong value, and others); 15 were caught, none of them by the sanitizer (these are logic errors, not memory errors). The one survivor is an equivalent change (a `return` of a variable that is always 0 at that point). Statements about the C standard's clause numbers, the Linux kernel, other compilers and other CPUs are from memory or from the standard and are marked.

**Data and timing.** Machine: a cloud container, Intel Xeon 2.10 GHz, GCC 13.3.0, `-O2` for timings, 2 cores. Times are the best of 3 repetitions, shown as run 1 / run 2, except the two-thread counter, which is a single run per figure (it starts threads, so best-of-3 would not mean the same thing). Data is random from a seeded generator, so sums are the same every run. Timings show the shape of the cost on this machine; the ratios will differ on yours.

---

## 1. `const`

`const` marks an object, or a view of an object, as read-only. The compiler rejects direct writes through a `const` lvalue and may place a `const` object with static storage in read-only memory. Where the `const` sits decides what is protected. Read a declaration from the name outward and treat `const` as applying to what is **immediately to its left** (or to the type on its right when nothing is on its left):

| Declaration | Meaning | `*p = 1` | `p = &y` |
|---|---|---|---|
| `const int *p` (or `int const *p`) | pointer to a constant `int` | error | fine |
| `int *const p` | constant pointer to `int` | fine | error |
| `const int *const p` | constant pointer to a constant `int` | error | error |

The compiler agrees. This function takes one parameter of each kind and tries the four forbidden writes plus the classic conversion; these are its real diagnostics:

```c
void demo(int *a, const int *b, int *const c, const int *const d) {
    *b = 1;                  /* error: the int is const through b          */
    b = a;                   /* fine: b itself is not const                */
    c = a;                   /* error: c itself is const                   */
    *c = 1;                  /* fine: the int is not const through c       */
    d = a;                   /* error: d itself is const                   */
    *d = 1;                  /* error: the int is const through d          */
    const char *cs = "x";
    char **bad = &cs;        /* warning: const char ** is not char **      */
    (void)bad;
}
```

```text
p1_const_errors.c: In function 'demo':
p1_const_errors.c:2:8: error: assignment of read-only location '*b'
    2 |     *b = 1;                  /* error: the int is const through b          */
p1_const_errors.c:4:7: error: assignment of read-only parameter 'c'
    4 |     c = a;                   /* error: c itself is const                   */
p1_const_errors.c:6:7: error: assignment of read-only parameter 'd'
    6 |     d = a;                   /* error: d itself is const                   */
p1_const_errors.c:7:8: error: assignment of read-only location '*(const int *)d'
    7 |     *d = 1;                  /* error: the int is const through d          */
p1_const_errors.c:9:18: warning: initialization of 'char **' from incompatible pointer type 'const char **' [-Wincompatible-pointer-types]
    9 |     char **bad = &cs;        /* warning: const char ** is not char **      */
```

Line 3 and line 5 of the source are accepted. The last line is the important warning: **`const char **` is not convertible to `char **`**, nor the other way round. If it were allowed, you could store a `const char *` through the `char **`, then modify the string through an ordinary `char *`. Adding `const` at the first level (`char *` to `const char *`) is implicit and safe; adding it at a deeper level is not. This is why `strtol(const char *s, char **end, int base)` takes a plain `char **` in the standard (a known hole: the function returns a pointer into a `const` string through an unqualified pointer), and why the parser below uses `const char **end`, which is the honest signature when the input is not modified.

```c
/* Const-correct parsing: the input is never modified, so it is const; the output
   position is a pointer into the input, so it is const char *, and the parameter that
   receives it is const char **. */
const char *skip_spaces(const char *s) {
    while (*s == ' ' || *s == '\t') ++s;
    return s;
}

/* Parses an unsigned decimal number after optional blanks.
   Returns 0 on success, -1 if there are no digits, -2 on overflow of uint32_t.
   *end is set to the first character that was not consumed. */
int parse_u32(const char *s, const char **end, uint32_t *out) {
    s = skip_spaces(s);
    if (*s < '0' || *s > '9') { *end = s; return -1; }
    uint64_t v = 0;
    int overflow = 0;
    for (; *s >= '0' && *s <= '9'; ++s) {
        v = v * 10 + (uint64_t)(*s - '0');
        if (v > UINT32_MAX) { overflow = 1; v = (uint64_t)UINT32_MAX + 1; }   /* stays large, cannot wrap */
    }
    *end = s;
    if (overflow) return -2;
    *out = (uint32_t)v;
    return 0;
}

/* Splits "a,b,c" without modifying it: returns the start of the next field and its
   length. Returns NULL at the end of the string. *rest is where to continue. */
const char *next_field(const char *s, char sep, size_t *len, const char **rest) {
    if (*s == '\0') return NULL;
    const char *e = s;
    while (*e && *e != sep) ++e;
    *len = (size_t)(e - s);
    *rest = *e ? e + 1 : e;
    return s;
}
```

`skip_spaces` takes and returns `const char *`: it neither modifies the input nor lets the caller think it may. `parse_u32` reports through `*end` where it stopped, and the result of a failed or overflowing parse never wraps, because the running value is a `uint64_t` that is clamped above the limit and so can never overflow itself (the tests compare every result and every end position with `strtoull`). `next_field` splits `a,b,c` without a single write to the input and without a terminator inserted by `strtok`, which is why it can be used on a string literal and on a buffer shared with other code.

### 1.1 What writing to a `const` object does

Casting away `const` and writing to an object that was **defined** `const` is undefined behaviour (it is legal to cast away `const` and write only if the object itself was never `const`). The compiler is entitled to treat the object as unchangeable, and you can see it do so:

```c
#include <stdio.h>

const int global_const = 5;              /* lives in read-only memory */

int main(int argc, char **argv) {
    (void)argv;
    const int local_const = 1;
    int *p = (int *)&local_const;        /* the cast removes const; writing is undefined behaviour */
    *p = 2;
    printf("local_const = %d, *p = %d\n", local_const, *p);
    if (argc > 1) {
        int *q = (int *)&global_const;
        *q = 6;                          /* writes to read-only memory */
        printf("not reached\n");
    }
    printf("done\n");
    return 0;
}
```

```text
-O0:
local_const = 2, *p = 2
done
-O2:
local_const = 1, *p = 2
done
```

At `-O0` the local `const` really changes: `2` and `2`. At `-O2` the compiler has folded `local_const` to the constant 1 and never reads it again, while the write through `p` still happens: `1` and `2`. The same program, two answers; both are valid because the behaviour is undefined. A global `const int` is worse, because it normally lives in a read-only section of the executable (`.rodata`), and writing there faults:

```text
-O2 with a write to the global const object:
(Segmentation fault)
exit status 139
```

Exit status 139 is 128 plus signal 11, the `SIGSEGV` from C 03. String literals are the same: `char *s = "abc"; s[0] = 'x';` writes to read-only memory (C 02), which is why you should declare such pointers `const char *`.

### 1.2 What `const` is and is not

- **It is** a compile-time check that stops accidental writes, documentation of intent in an API (`const char *` means "I will not modify it"), and a hint that lets static data go to read-only memory. On a microcontroller that is the difference between a table in flash and a table copied into scarce RAM (Embedded track).
- **It is not** "this value never changes". A `const int *p` only promises that *you* will not write through `p`; another pointer may modify the same object, so the optimiser cannot assume the value is stable (only an object defined `const` can be assumed unchanged).
- **It is not** a constant expression in C. `const int n = 5; int a[n];` declares a variable-length array in C (C++ accepts it as a true constant). Use `enum { N = 5 };` or `#define N 5` when you need a constant.
- **`const volatile`** is meaningful: a read-only hardware status register is `const volatile uint32_t *`: you must not write it, and the hardware may change it behind your back (section 2).
- **A top-level `const` on a parameter** (`void f(int *const p)`) is not part of the function's type and only constrains the body. Const-correctness lives in `const T *` parameters.
- **The `strchr` hole.** `char *strchr(const char *s, int c)` takes a `const char *` and returns a `char *` into the same string, so the compiler lets you launder the `const` away without a cast:

```c
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *s = "hello";             /* a string literal: read-only memory */
    char *p = strchr(s, 'l');            /* compiles: strchr takes const char * and returns char * */
    printf("found at index %td\n", p - s);
    fflush(stdout);
    *p = 'L';                            /* writes to the literal */
    printf("not reached\n");
    return 0;
}
```

```text
found at index 2
(Segmentation fault)
exit status 139
```

The program compiles without a warning and crashes at the write. C23 adds type-generic versions so that a `const` input gives a `const` result (from memory); in C11, write your own wrapper or copy the string first. C++ solves this with two overloads.

## 2. `volatile`

`volatile` tells the compiler that an object may change, or that reading and writing it has an effect, in a way the program text does not show. Every access to a `volatile` object in the source must happen in the compiled program: it may not be removed, merged with another, cached in a register, or invented, and accesses to different volatile objects keep their relative order. Three legitimate uses:

1. **Memory-mapped I/O registers.** `volatile uint32_t *UART_DR = (volatile uint32_t *)0x4000C000;`. Reading a status register twice is two bus reads; writing a command register twice is two writes. Without `volatile` the optimiser would merge them.
2. **Objects modified by a signal handler or an interrupt service routine** in the same thread of execution: the type must be `volatile sig_atomic_t` for a flag (the type the C standard guarantees for this; lock-free atomic types also work).
3. **Locals that live across `setjmp`/`longjmp`** (C 10).

This is what happens if you leave it out. The program waits for a flag set by a signal handler (`alarm(1)` sends `SIGALRM` after a second):

```c
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#ifdef USE_VOLATILE
static volatile sig_atomic_t done;
#else
static int done;                         /* a plain int: the compiler may keep it in a register */
#endif

static void on_alarm(int sig) { (void)sig; done = 1; }

int main(void) {
    signal(SIGALRM, on_alarm);
    alarm(1);
    while (!done)
        ;                                /* wait for the handler */
    printf("flag seen\n");
    return 0;
}
```

```text
plain int, -O2:
exit status 124
volatile sig_atomic_t, -O2:
flag seen
exit status 0
```

With a plain `int` the program is still running when `timeout` kills it after three seconds (status 124): the optimiser saw that the loop never changes `done`, so it read the flag once and turned the loop into an infinite one. With `volatile sig_atomic_t` it returns after a second. The generated code shows exactly why; this is a function that does nothing but wait on a flag, compiled with `-O2`:

```c
extern int done_plain;
extern volatile int done_vol;

void wait_plain(void) { while (!done_plain) ; }
void wait_volatile(void) { while (!done_vol) ; }
```

```text
wait_plain:
.LFB0:
    endbr64
    movl    done_plain(%rip), %eax
    testl    %eax, %eax
    jne    .L1
.L3:
    jmp    .L3
.L1:
    ret
.LFE0:
wait_volatile:
.LFB1:
    endbr64
.L6:
    movl    done_vol(%rip), %eax
    testl    %eax, %eax
    je    .L6
    ret
.LFE1:
```

`wait_plain` loads `done_plain` **once**, tests it, and either returns or falls into `.L3: jmp .L3`, a loop that never reads memory again. `wait_volatile` jumps back to the `movl` that reloads the flag on every iteration. (The `endbr64` is a control-flow protection instruction GCC emits on Ubuntu by default; ignore it.)

### 2.1 What `volatile` does not give you

- **No atomicity.** `volatile long n; n++;` is a read, an add and a write, three separate steps. Two threads (or a thread and an ISR doing read-modify-write on the same variable) interleave those steps and lose updates.
- **No ordering between threads or CPUs.** `volatile` orders accesses to volatile objects as the compiler emits them; it says nothing about non-volatile accesses around them, and it emits no memory-barrier instruction. The CPU may still reorder visible effects (section 3).
- **No memory visibility guarantee.** A volatile write in one thread need not be seen by another thread in any particular order or time.

The trace shows the first point. Two threads each add 1 to a counter 5,000,000 times, once with a `volatile long` and once with a C11 `_Atomic long`:

```text
T3 two threads, 5000000 increments each, expected total 10000000: volatile counter lost updates: yes; atomic counter exact: yes
```

| Two threads, 5,000,000 increments each | Time ms (single runs, run 1 / run 2) |
|---|---|
| `volatile long` counter, `v = v + 1` | 12.95 / 18.33 |
| `_Atomic long` counter, `fetch_add` | 269.36 / 207.32 |

The volatile counter lost updates (its final value was below 10,000,000; the exact value changes from run to run, so the trace prints only the verdict) and the atomic counter was exact. The volatile version looks faster only because it is wrong: no thread waits for the cache line, so most of the work is thrown away. The atomic version is about 11 to 21 times slower here because the two cores fight over one cache line, and it is correct. The fix for thread-shared data is `_Atomic`, a mutex or another synchronisation primitive (C 10, OS 06 to OS 08), never `volatile`. The old pattern `volatile int flag` for a producer/consumer handoff "works" on x86 for simple cases and fails on weaker memory models and when the data guarded by the flag is itself not ordered.

### 2.2 What `volatile` costs

```text
T2 sum of 16000000 ints: plain 7991065522, through a volatile pointer 7991065522, equal: yes
```

| Sum 16,000,000 ints | Time ms (run 1 / run 2) |
|---|---|
| through an ordinary pointer | 7.55 / 7.27 |
| through a `volatile` pointer | 8.93 / 10.64 |

The volatile loop is 1.2 to 1.5 times slower here: every element is loaded exactly as written, in order, without vectorisation or unrolling tricks. For a register access the cost is irrelevant (the bus access dominates); for a bulk loop over ordinary memory it is a reason not to mark data `volatile` "to be safe".

## 3. Compiler barriers, `READ_ONCE` and `WRITE_ONCE`

A **compiler barrier** is a point the compiler may not move memory accesses across. It generates no instruction. In GCC and Clang it is an empty `asm` statement with a `memory` clobber, which tells the compiler "this may read or write any memory", so values cached in registers must be written back before it and reloaded after it. A **CPU (hardware) barrier** (`mfence` on x86, `dmb` on ARM, `smp_mb()` in the kernel) is a real instruction that orders the memory operations the processor itself performs. The two solve different problems, and a compiler barrier does not stop the CPU reordering anything.

The Linux kernel does not use `volatile` on variables for shared data. It uses accessors that apply `volatile` to **one access** instead:

```c
/* One access, exactly once, not merged, not invented, not reordered with other
   volatile accesses. These are the Linux kernel's definitions in spirit (GCC/Clang). */
#define READ_ONCE(x)       (*(const volatile __typeof__(x) *)&(x))
#define WRITE_ONCE(x, v)   (*(volatile __typeof__(x) *)&(x) = (v))

/* A compiler barrier: emits no instruction; the compiler may not move memory accesses
   across it or keep memory values in registers across it. It is NOT a CPU barrier. */
#define barrier()          __asm__ volatile("" ::: "memory")
```

`READ_ONCE(x)` reads `x` through a `const volatile` view exactly once, with the type of `x` (hence `__typeof__`); `WRITE_ONCE(x, v)` writes once. Together they prevent the compiler from **tearing** (splitting a 64-bit access in two), **fusing** (merging two reads into one), **re-fetching** (reading the variable again where the source reads it once) and **inventing** accesses. The re-fetch case matters. In this kind of code, where another CPU may change `x` at any moment:

```text
if (x < limit)      /* check */
        table[x] = 1;   /* use: the compiler may read x again here, and get a different value */
```

nothing in the source says `x` is read once, so a compiler is allowed to read it twice, and a concurrent change between the two reads gives an out-of-range index. `int v = READ_ONCE(x); if (v < limit) table[v] = 1;` has a single read and a local copy. These macros document intent in the code (this variable is shared and accessed without a lock) and give an exact load or store. They still do not make a read-modify-write atomic, and they do not order against other CPUs (that is what atomics with memory orders, `smp_load_acquire`, and barriers are for; C 10 and the Kernel track).

## 4. `restrict`

`restrict` (C99), written on a pointer, is a **promise by the programmer**: for the lifetime of that pointer, any object it points to that is modified is accessed only through that pointer (or through pointers derived from it). With the promise, the compiler may reorder, merge and keep values in registers because stores through one pointer cannot change what another pointer reads. The standard library uses it: `void *memcpy(void *restrict dst, const void *restrict src, size_t n)` promises that the two ranges do not overlap, which is why `memmove` exists for the case where they do.

```c
/* restrict is a promise from the caller: during this call, nothing reached through
   dst is also reached through a or b. The compiler may rely on it. */
void vec_add(int *restrict dst, const int *restrict a, const int *restrict b, size_t n) {
    for (size_t i = 0; i < n; ++i) dst[i] = a[i] + b[i];
}

/* The same loop without the promise: the compiler must assume dst may overlap a or b. */
void vec_add_alias(int *dst, const int *a, const int *b, size_t n) {
    for (size_t i = 0; i < n; ++i) dst[i] = a[i] + b[i];
}

/* A scale factor read through a pointer: without restrict the compiler has to reload
   *k after every store to dst, because dst might point at k. */
void scale(int *restrict dst, const int *restrict src, const int *restrict k, size_t n) {
    for (size_t i = 0; i < n; ++i) dst[i] = src[i] * *k;
}

void scale_alias(int *dst, const int *src, const int *k, size_t n) {
    for (size_t i = 0; i < n; ++i) dst[i] = src[i] * *k;
}
```

The compiler does not check the promise. If it is broken, the behaviour is undefined, and the symptom depends on what the optimiser did with the assumption. This program copies a vector onto itself shifted by one, which breaks the promise on purpose:

```c
#include <stdio.h>

__attribute__((noinline)) static void shift_add(int *restrict dst, const int *restrict src, int n) {
    for (int i = 0; i < n; ++i) dst[i] = src[i] + 1;
}

int main(void) {
    int x[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    shift_add(x + 1, x, 8);              /* dst and src overlap: the promise is broken */
    for (int i = 0; i < 10; ++i) printf("%d ", x[i]);
    printf("\n");
    return 0;
}
```

```text
-O0:
0 1 2 3 4 5 6 7 8 0
-O2:
0 1 1 1 1 2 1 1 1 0
```

At `-O0` the loop runs one element at a time, so each store feeds the next load: `0 1 2 3 4 5 6 7 8 0`. At `-O2` the compiler assumed the ranges were separate, loaded several source elements at once with a vector instruction, and wrote the results together: `0 1 1 1 1 2 1 1 1 0`. Neither answer is wrong, because the program has no defined answer. A broken `restrict` is a bug that appears only in optimised builds and differs between compiler versions, which makes it hard to find; AddressSanitizer does not catch it.

Does `restrict` make code faster? I measured the two kernels with and without it on 16,000,000 elements:

```text
T1 16000000 elements: vec_add with restrict 15982526419, without 15982526419; scale with restrict 23979113400, without 23979113400; all agree: yes
```

| 16,000,000 elements | Time ms (run 1 / run 2) |
|---|---|
| `vec_add` with `restrict` | 12.75 / 13.11 |
| `vec_add_alias` without | 12.44 / 12.72 |
| `scale` with `restrict` (reads `*k` through a pointer) | 9.20 / 9.80 |
| `scale_alias` without | 8.95 / 10.15 |

**No measurable difference** on this compiler and CPU: both versions are limited by memory bandwidth, and GCC at `-O2` handles the unannotated loops well (it can check at run time whether the ranges overlap and use the fast loop when they do not; I did not inspect the assembly to confirm that here, so treat the reason as likely, not proven). `restrict` helps most when the compiler cannot do that: loops over many pointers, loops containing calls, or code where an unnoticed store forces a reload of a value inside a hot loop. Use it on the parameters of performance-critical kernels that you have measured, document the no-overlap requirement in the comment, and do not sprinkle it around: a wrong `restrict` costs far more than a right one saves.

## 5. Storage classes and linkage

### 5.1 Two meanings of `static`

`static` means two different things depending on where it appears, and mixing them up is a classic interview trap.

- **At function (block) scope:** the variable has **static storage duration**: one object for the whole run, created before `main`, keeping its value between calls. (Duration is C 03, section 1.)
- **At file scope** (on a function or a variable): the name has **internal linkage**: it is visible only inside this translation unit (this `.c` file after preprocessing). Another file can define its own `hidden` without conflict.

**Linkage** is the property that says whether two declarations of one name, in the same or different files, refer to the same thing:

| Linkage | Names | Same name in another file is... |
|---|---|---|
| **None** | local variables, parameters, `typedef` names, struct tags | unrelated |
| **Internal** | `static` file-scope variables and functions (and `const` globals in C++) | a different object |
| **External** | file-scope variables and functions without `static`; anything declared `extern` | the same object |

### 5.2 `extern`, declarations and definitions

A **declaration** says a name exists and has a type; a **definition** also makes the storage (or the function body). A name may be declared many times and must be defined exactly once among all files (the **one-definition rule** at link time). `extern int shared;` is a declaration; `int shared = 10;` at file scope is the definition. Functions work the same way: the prototype `int next_a(void);` declares, the body defines (`extern` is implicit on function declarations).

The convention that keeps this correct: **declare in the header with `extern`; define in exactly one `.c` file; include the header in that `.c` file** so that the compiler checks that the declaration and the definition agree. This is a small multi-file program, compiled and run:

```c
#ifndef COMMON_H
#define COMMON_H
extern int shared;                     /* declaration: no storage here */
int next_a(void);
int next_b(void);
const int *hidden_a(void);
const int *hidden_b(void);

static int bump(void) {                /* static in a header: every file that includes it gets its own copy */
    static int n;
    return ++n;
}

static inline int sq(int x) { return x * x; }   /* static inline: safe in a header */
#endif
```

```c
#include <stdio.h>
#include "common.h"

int shared = 10;                       /* external linkage: defined here, visible everywhere */
static int hidden = 1;                 /* internal linkage: this file only */
const int table_a[] = {1, 2, 3};       /* read-only data, external linkage */
static const int stable_a[] = {4, 5};  /* read-only data, internal linkage */
const char *const names_a[] = {"x", "y"};

static int counter(void) {             /* internal function with a static local */
    static int calls;
    return ++calls;
}

int next_a(void) { return counter() + bump() * 100; }
const int *hidden_a(void) { return &hidden; }
int sum_tables(void) { return table_a[0] + stable_a[0] + (int)sizeof names_a[0]; }
```

```c
#include "common.h"

static int hidden = 2;                 /* a different object with the same name */

int next_b(void) { return bump() * 100 + sq(3); }
const int *hidden_b(void) { return &hidden; }
void set_shared(int v) { shared = v; }
```

```c
#include <stdio.h>
#include "common.h"

void set_shared(int v);

int main(void) {
    int a1 = next_a(), a2 = next_a();
    int b1 = next_b(), b2 = next_b();
    printf("next_a: %d %d\n", a1, a2);
    printf("next_b: %d %d\n", b1, b2);
    printf("bump in main.c: %d (its own copy)\n", bump());
    printf("hidden in a.c = %d, in b.c = %d, same object: %d\n", *hidden_a(), *hidden_b(), hidden_a() == hidden_b());
    set_shared(42);
    printf("shared = %d (one object seen by all files)\n", shared);
    return 0;
}
```

```text
next_a: 101 202
next_b: 109 209
bump in main.c: 1 (its own copy)
hidden in a.c = 1, in b.c = 2, same object: 0
shared = 42 (one object seen by all files)
```

Read the output against the source. `hidden` exists twice, in `a.c` and `b.c`, with different values, at different addresses (`same object: 0`): internal linkage. `shared` exists once; `set_shared` in `b.c` changes the object that `main.c` reads. `bump` is a `static` function with a `static` local, **defined in a header**; every file that includes the header gets its own copy of both, so `a.c`, `b.c` and `main.c` each count from 1 independently (`next_a` returns 101 and 202, `next_b` 109 and 209, `main` 1). That is legal and useful for tiny helpers, and a bug if you meant one shared counter.

### 5.3 What the linker sees: `nm`

`nm` lists the symbols of an object file; the letter says which section and whether the symbol is visible outside it (uppercase = external, lowercase = local, internal). This is `nm a.o` after `gcc -c a.c`, reduced to the symbol type and name:

```text
t bump
b calls.0
t counter
d hidden
T hidden_a
b n.1
D names_a
T next_a
D shared
r stable_a
T sum_tables
R table_a
```

| Letter | Meaning | Examples above |
|---|---|---|
| `T` / `t` | code (text): external / internal function | `next_a`, `sum_tables` / `counter`, `bump` |
| `D` / `d` | initialised data: external / internal variable | `shared`, `names_a` / `hidden` |
| `B` / `b` | zero-initialised data (`.bss`): external / internal | `calls.0` and `n.1` (the function-scope statics, with a suffix from the compiler) |
| `R` / `r` | read-only data: external / internal | `table_a` / `stable_a` |
| `U` | undefined: used here, defined elsewhere | (none in this file) |

Two details worth knowing. `names_a` is `const char *const names_a[]`, an array of constant pointers, and shows as `D` rather than `R`: on a position-independent build the addresses inside it need relocation by the loader, so it cannot be read-only (`.data.rel.ro` in the object file; from memory of how GCC places it). And a `static` function that nothing calls is removed by the optimiser and gets a `-Wunused-function` warning; an external function is always kept. That is a practical reason for the rule **make everything `static` unless another file needs it**: the interface of a module becomes exactly its `T` symbols, the optimiser can inline and delete freely, and name clashes between files become impossible.

### 5.4 Linker errors you should recognise

Three link failures every C programmer meets, with the real messages.

**Two definitions.** `int total = 1;` in one file and `int total = 2;` in another:

```text
OBJ:(.data+0x0): multiple definition of `total'; OBJ:(.data+0x0): first defined here
collect2: error: ld returned 1 exit status
```

**A tentative definition in a header.** The line `int counter_total;` with no `extern` and no initialiser, put in a header that two files include, used to link on GCC (older C allowed these "tentative definitions" to merge, the **common** model). GCC 10 changed the default to `-fno-common`, so each include is now a real definition:

```text
OBJ:(.bss+0x0): multiple definition of `counter_total'; OBJ:(.bss+0x0): first defined here
collect2: error: ld returned 1 exit status
--- with -fcommon:
links, exit status 1
```

With `-fcommon` it links and runs (exit status 1, the two files shared one variable). The fix is not the flag; it is `extern int counter_total;` in the header and a definition in one `.c`.

**Using a `static` function from another file.** The name does not exist outside `undef_a.c`, so the other file's call cannot be resolved:

```text
OBJ: in function `main':
undef_b.c:(.text+0x9): undefined reference to `secret'
collect2: error: ld returned 1 exit status
```

This is the same message you get for a typo, a forgotten source file in the build, or a missing library (`-lm`); the `undefined reference to` text names the symbol and the object that wanted it.

Linkage is about the **linker**. At the next level, **visibility** (`-fvisibility=hidden` and `__attribute__((visibility("default")))`) decides which external symbols a shared library exports (C 06). In C++, `const` globals have internal linkage by default and `extern "C"` turns off name mangling; neither applies to C.

## 6. `inline`

`inline` suggests that calls to a function be replaced by its body. In C99 and later the rules are different from C++, and the difference shows up as a link error.

- A function declared `inline` **without** `extern` in a header gives an **inline definition**: the compiler may use it to inline a call, but it does **not** emit a callable function. If the compiler decides not to inline a call (always the case at `-O0`), the call refers to an external function that must exist somewhere.
- Exactly one translation unit must provide that external definition, by repeating the declaration with `extern`: `extern inline int twice(int x);`.
- `static inline` sidesteps all of this: each file gets a private function that may be inlined or kept. It is the form used in headers (the Linux kernel's headers are full of `static inline` functions, from memory).

```c
inline int twice(int x) { return 2 * x; }   /* C99 inline definition: no external definition is emitted */
```

```text
-O0, no extern definition:
inl1.c:(.text+0x19): undefined reference to `twice'
collect2: error: ld returned 1 exit status
-O2:
links at -O2, exit status 2
-O0 with the extern inline definition in inl2.c:
links, exit status 2
```

At `-O0` the call to `twice` stays a call and the link fails with `undefined reference to 'twice'`, because `inl.h` only offers an inline definition. At `-O2` the call is inlined and the program links; **the same source builds in one configuration and not the other**, a classic trap. Adding `inl2.c`, which holds `extern inline int twice(int x);` after including the header, supplies the one external definition and fixes `-O0`.

`inline` is a hint. GCC inlines by its own cost model whatever you write, and `__attribute__((always_inline))` and `__attribute__((noinline))` are the commands. The cost of not inlining a tiny function is small per call and large in a tight loop:

```text
T4 400000000 iterations: inlined sum 1400000000, called sum 1400000000, equal: yes
```

| 400,000,000 calls of `a + b` | Time ms (run 1 / run 2) |
|---|---|
| inlined | 102.66 / 114.55 |
| `noinline` call | 224.85 / 239.31 |

The call is about twice as slow in this loop (the loop body is a single add, so the call overhead is nearly all of it). Real functions do more work per call, so do not inline by hand for speed; inline small accessors, getters and helpers in headers, and let the compiler decide the rest. Over-inlining grows the code and can slow things down by filling the instruction cache.

## 7. Static locals, the buffer trap

A function-scope `static` buffer is a favourite way to return a string without allocating. It has a cost the caller never sees:

```c
#include <stdio.h>

static const char *fmt(int v) {
    static char buf[16];                 /* one buffer, shared by every call */
    snprintf(buf, sizeof buf, "<%d>", v);
    return buf;
}

int main(void) {
    const char *a = fmt(1);
    const char *b = fmt(2);
    printf("a = %s, b = %s, same pointer: %d\n", a, b, a == b);
    return 0;
}
```

```text
a = <2>, b = <2>, same pointer: 1
```

Both calls returned the same pointer; the second call overwrote the first result, so `a` now reads `<2>` as well. The same hazard exists across threads (two threads share the buffer), inside a recursive call and inside a signal handler. Functions with this property are not **reentrant** and not **thread safe** (`strtok`, `asctime`, `localtime` and `gmtime`, `getenv` in some uses; C 07 covers the standard library). Fixes, in order of preference: the caller supplies the buffer and its size (`snprintf` style), the function allocates and the caller frees, or the function returns a struct by value. `_Thread_local` makes the buffer per thread and still leaves the overwrite between two calls in one thread.

## 8. What it costs

| Question | Answer (this machine, GCC 13.3, `-O2`) |
|---|---|
| `const` at run time | free; it is a compile-time property (it can move data to read-only memory) |
| Writing to a `const` object | undefined; `SIGSEGV` for a global, a value that changes with `-O0` against `-O2` for a local |
| `volatile` loads in a bulk loop | 1.2 to 1.5 times slower than ordinary loads |
| `volatile` as a thread counter | wrong: updates lost; `_Atomic` is exact and about 11 to 21 times slower under contention |
| `restrict` on two memory-bound kernels | no measurable difference; a broken promise changes the result |
| `static inline` helper in a header | free to use; one private copy per file |
| Out-of-line call of an add, 400,000,000 times | about 2 times slower than inlined |
| A static buffer returned from a function | free; shared by every call and every thread |

## 9. Common mistakes

1. **Casting away `const` and writing.** Undefined behaviour; the result depends on the optimisation level (section 1.1). Copy the data if you need to modify it.
2. **`char *` for string literals.** Declare them `const char *`; the write fails at run time, not at compile time.
3. **`volatile` for thread synchronisation.** No atomicity, no ordering. Use `_Atomic`, a mutex or a condition variable.
4. **A flag set by a handler or ISR that is not `volatile`.** The loop reading it is compiled to an infinite loop (section 2). Use `volatile sig_atomic_t` for signals.
5. **`volatile` on ordinary data "to be safe".** It slows the code, blocks optimisation and fixes nothing.
6. **A broken `restrict` promise.** Overlapping arguments to a function declared with `restrict` (or to `memcpy`) are undefined behaviour; use `memmove`, or remove the annotation.
7. **`int x;` in a header.** Each include is a definition; the link fails with `multiple definition`. Write `extern int x;` in the header and define it once.
8. **Defining a variable in a header with an initialiser.** Same problem, and silent copies if it is `static` (every file gets its own).
9. **A non-`static` helper function in a `.c` file.** It becomes part of the global namespace and can collide with another file's helper. Default to `static`.
10. **`inline` in a header without `static` or an external definition.** Links at `-O2`, fails at `-O0` (section 6).
11. **Returning a pointer to a `static` buffer, and calling the function twice in one expression.** The first result is overwritten (section 7).
12. **Assuming `const int n = 5; int a[n];` is a fixed-size array.** In C it is a variable-length array; use an `enum` constant.

## 10. Questions out loud

1. **What is the difference between `const int *p`, `int *const p` and `const int *const p`?** Pointer to constant data (cannot write through it); constant pointer to modifiable data (cannot repoint it); both constant. Read from the name outward.
2. **Is it safe to cast away `const`?** Only if the underlying object was not defined `const`. Writing to a `const`-defined object is undefined: read-only memory for globals and literals, an optimiser-dependent value for locals.
3. **What does `volatile` mean, and when do you need it?** The compiler must perform every access as written and in order. Needed for memory-mapped registers, objects changed by a signal handler or ISR, and locals across `longjmp`.
4. **Is `volatile` enough for a shared flag between threads?** No: it gives no atomicity and no ordering with other memory. Use atomics with the right memory order, or a lock.
5. **What is the difference between a compiler barrier and a memory barrier?** A compiler barrier stops the compiler reordering or caching memory accesses and emits no instruction. A memory barrier is an instruction that orders what the CPU does to memory. Concurrent code usually needs the second; the first is for the optimiser.
6. **What are `READ_ONCE` and `WRITE_ONCE`?** Kernel macros doing one volatile access with the variable's own type, preventing tearing, merging and re-fetching without making the variable volatile everywhere.
7. **What does `restrict` promise, and what if you lie?** That objects accessed through the pointer are not accessed through other pointers in its scope. If you lie, the behaviour is undefined, and in practice the result changes with the optimisation level.
8. **What are the two meanings of `static`?** Block scope: static storage duration. File scope: internal linkage. Different properties, the same keyword.
9. **What is the difference between a declaration and a definition?** A declaration introduces a name and its type; a definition also allocates the storage or provides the body. One definition across the program; any number of declarations.
10. **Why does `int x;` in a header break the link?** It is a definition in every file that includes it; with `-fno-common` (the default since GCC 10) the linker sees several. Use `extern` in the header and one definition in a `.c` file.
11. **What does `inline` mean in C, and how does it differ from C++?** A hint, plus a rule: a plain `inline` function in a header is an inline definition only and needs one external definition somewhere (`extern inline`), otherwise the link fails when a call is not inlined. `static inline` avoids the issue. In C++ the compiler merges the copies for you.
12. **Why is returning a `static` buffer a bad idea?** Every call returns the same pointer, so results overwrite each other, and it is not thread safe. Pass a buffer in, or allocate.

---

## Problems

Difficulty is marked E (entry level), M (medium) or H (hard). Do A to C on paper first.

**A (E, on paper).** For each declaration, say what is constant, and which of `*p = 1;` and `p = &y;` compile: `const int *p;`, `int const *p;`, `int *const p = &x;`, `const int *const p = &x;`. Then say why `const int **pp = &p;` (with `p` an `int *`) is rejected and what could go wrong if it were allowed.

**B (E, on paper).** What is wrong with this code, and what happens at run time? `const char *s = "hello"; char *p = strchr(s, 'l'); *p = 'L';` Why does it compile without a warning? Give two ways to write the intent safely.

**C (M, on paper).** `volatile long n;` is incremented 5,000,000 times by each of two threads with `n++;`. Write the three machine-level steps of `n++`, show one interleaving that loses an update, and list two correct fixes. Why does the observed total differ from run to run?

**D (M, LeetCode).** *String to Integer (atoi)* (8) and *Reverse Integer* (7), both from memory. The common bug in both is detecting overflow after it happened. Compare with `parse_u32` in section 1, which widens to 64 bits and clamps; write the check that detects overflow before the multiply for a 32-bit signed result (compare against `INT_MAX / 10` first).

**E (H, in your editor).** Build a small module `idgen` in three files: `idgen.h` (the API), `idgen.c` (static state, an `idgen_next()` function and one internal helper) and `main.c`. Compile with `-Wall -Wextra -Wmissing-prototypes -Wunused-function`, run `nm idgen.o`, and show that the only `T` symbols are the API; then add `-fvisibility=hidden` and explain what it changes for a shared library. Then break it on purpose three ways (a definition in the header, a non-`static` helper that collides with one in `main.c`, a missing `extern`) and write down each linker message.

**Where to solve them.** A to C on paper first, then check against the answers. D on LeetCode (7 and 8; the numbers and constraints are from memory, so check the statements). E in your editor with `gcc` and `nm`; the programs in section 5 are the template.

**How to approach them.**

- **A:** read each declaration from the name outward; `const` applies to what is on its left, or to the type on its right when it is first.
- **B:** follow the types: `strchr` returns `char *`; then ask where the characters of a string literal live.
- **C:** `n++` on a `volatile` is load, add one, store. Two threads can both load the same value.
- **D:** write the overflow check as a comparison of the current value with `INT_MAX / 10` before multiplying, not after.
- **E:** start from the working three files, run `nm`, then break one thing at a time and read the linker message before changing anything.

## Answers

**A.** `const int *p` and `int const *p` are the same: the `int` is constant, the pointer is not, so `*p = 1;` is an error and `p = &y;` compiles. `int *const p = &x;` is the opposite: `*p = 1;` compiles, `p = &y;` is an error. `const int *const p` rejects both. The compiler's own messages for these cases are in section 1 above. `const int **pp = &p;` with `int *p` is rejected because, if it were allowed, you could write `*pp = &constant_int;` (a `const int *` stored through the `const int **`, which is legal for that type) and `p` would then point at a constant object through a plain `int *`, and `*p = 5;` would modify a constant.

**B.** `strchr` takes a `const char *` and returns a `char *` into the same string, so the compiler accepts the assignment without a cast or a warning. The string literal lives in read-only memory, so the write faults. The program below finds the `l` and then crashes (exit status 139):

```text
found at index 2
(Segmentation fault)
exit status 139
```

Safe forms: `const char *p = strchr(s, 'l');` if you only read, or copy the string into an array (`char buf[] = "hello";`) if you want to write. C23 provides `const`-preserving versions (from memory).

**C.** `n++` is: (1) load `n` into a register, (2) add 1, (3) store the register to `n`. Thread 1 loads 100. Thread 2 loads 100. Thread 1 stores 101. Thread 2 stores 101. Two increments, one added: an update is lost. The total differs from run to run because it depends on when the two threads happen to overlap, which the scheduler and the cache decide. Fixes: an `_Atomic long` with `atomic_fetch_add`, or a mutex around the update. The trace shows both: the volatile counter lost updates, the atomic one was exact.

**D.** In 7 and 8 compute with a wider type (`long long`), or test before multiplying: if `result > INT_MAX / 10` or `result == INT_MAX / 10 && digit > 7`, return `INT_MAX`. For 8 the sign decides whether the limit is `INT_MAX` or `INT_MIN`. This is the same idea as `parse_u32`: the running value is clamped above the limit so that it can never wrap, and the check happens before the value is used.

**E.** An outline, not a full solution. `idgen.c`: `static unsigned next_id;` and `static unsigned scramble(unsigned x) { ... }`, and `unsigned idgen_next(void)` that uses them; `idgen.h` declares only `unsigned idgen_next(void);`. `nm idgen.o` shows `T idgen_next`, `t scramble`, `b next_id` (or `d`): one `T`. `-fvisibility=hidden` makes every external symbol hidden in the shared library unless you mark it `__attribute__((visibility("default")))`, so the library exports only the API. The three breakages give: a definition in the header, `multiple definition of ...`; a non-`static` helper with a name that also exists in `main.c`, `multiple definition of ...`; a missing `extern` on a variable in a header that two files include, `multiple definition` under GCC 10 or later.

## Cheat sheet

- **`const`:** immediately to its left; `const T *` (data fixed), `T *const` (pointer fixed). `T *` to `const T *` is implicit; `T **` to `const T **` is not. Writing to a `const` object: undefined (a global faults, a local depends on `-O`).
- **`volatile`:** every access happens, in order; for MMIO, signal flags (`volatile sig_atomic_t`) and `longjmp` locals. Not atomic, not ordered with other memory, no barrier. Not for threads.
- **`READ_ONCE` / `WRITE_ONCE`:** one volatile access with the variable's type (no tearing, merging, re-fetching). `barrier()` is `asm volatile("" ::: "memory")`: compiler only, no CPU effect.
- **`restrict`:** a promise of no aliasing; broken promise = undefined, differs by `-O` level. `memcpy` uses it, `memmove` does not.
- **`static`:** block scope = static storage duration; file scope = internal linkage. Default to `static` for anything another file does not need.
- **`extern`:** a declaration. Declare in the header, define once in a `.c` file, include the header where it is defined. `int x;` in a header breaks the link (`-fno-common`).
- **`nm` letters:** `T`/`t` code, `D`/`d` data, `B`/`b` bss, `R`/`r` read-only, `U` undefined; uppercase external, lowercase internal.
- **`inline` (C99):** a hint plus a rule: a plain `inline` in a header needs one `extern inline` definition or the link fails at `-O0`; `static inline` is the safe form.
- **Static buffers:** shared by every call and every thread; pass a buffer in instead.

## Sources

- [C11 draft N1570](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf): 6.2.2 (linkages of identifiers), 6.7.1 (storage-class specifiers), 6.7.3 (type qualifiers, including `volatile` and `restrict`), 6.7.4 (function specifiers, `inline`). Clause numbers are from memory; I did not re-open the PDF for this tutorial.
- [GCC manual: Extended Asm](https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html): the `memory` clobber used by the compiler barrier.
- [GCC manual: An Inline Function is As Fast As a Macro](https://gcc.gnu.org/onlinedocs/gcc/Inline.html): how GCC treats `inline`, `static inline` and `extern inline`.
- [Linux kernel: memory barriers](https://www.kernel.org/doc/Documentation/memory-barriers.txt): compiler barriers, CPU barriers, `READ_ONCE` and `WRITE_ONCE` (my description of the macros is from memory of this document).
- [nm(1) manual page](https://man7.org/linux/man-pages/man1/nm.1.html): the symbol type letters.
