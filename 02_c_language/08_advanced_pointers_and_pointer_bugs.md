# C 08: Advanced Pointers and Pointer Bugs

*Part of the [C language plan](00_plan.md) · Time: about 5 hours (3 reading, 2 problems) · You need: [C 02](02_pointers_and_arrays.md) for pointer basics, [C 03](03_memory_stack_heap_and_malloc.md) for the stack, the heap and the sanitizers, and [C 07](07_strings_stdlib_and_bit_manipulation.md) for the library functions that misbehave. There is no companion demo file: every program below was compiled and run, and its real output is printed under it*

Pointers are the part of C that decides whether you are trusted with a kernel, a driver or a network stack. This tutorial has two halves. The first is **reading and writing pointer code fluently**: declarations such as `void (*signal(int, void (*)(int)))(int)`, the four forms of `*p++`, pointers used as integers and tagged with spare bits, and function pointers used the way real systems code uses them (callbacks with a context pointer, jump tables, table-driven state machines). The second is **a catalogue of pointer bugs**, each a compiled program with the real report from AddressSanitizer, valgrind or glibc: null, uninitialised, dangling, use after free, double free, stale pointers after `realloc`, leaks, freeing the wrong thing, overflows, `malloc(0)`, aliasing, bus errors against segmentation faults, and stack exhaustion. You will see what each one looks like when it goes wrong, which tool names it, and what the repaired code is.

By the end you can:

- read any C declaration, including functions that return pointers to functions and arrays of them, and check your reading with a typedef chain;
- predict the result of `*p++`, `(*p)++`, `*++p` and `++*p`, and write the string-copy loop `while ((*d++ = *s++))`;
- name each of the 15 pointer bugs of section 3 from its report and say how to repair it;
- write a growable array that survives allocation failure, and explain why a pointer into it is invalid after a push;
- explain why a function that frees its argument cannot clear the caller's pointer, and use the `T **` form or a `FREE` macro;
- use `uintptr_t` for alignment arithmetic and for tagging pointers with spare low bits, and say when it is legal;
- explain strict aliasing with the compiled evidence, and `SIGBUS` against `SIGSEGV`;
- write callbacks with a context pointer, a jump table and a table-driven state machine, and say what an indirect call costs when the target is predictable and when it is not.

**Where the numbers come from.** Every program and command was run on an x86-64 Linux container (Ubuntu, glibc 2.39, GCC 13.3). Code is compiled with `-std=gnu11 -Wall -Wextra -Wno-unused-function` (the header defines several `static` helpers and each demo uses only some). Bug demos are compiled with `-g -O0 -fsanitize=address -fno-omit-frame-pointer` unless the text says otherwise. The helper code printed in this tutorial (alignment and tagged pointers, the `FREE` macro, a growable array, a generic heapsort with a context argument, and a table-driven state machine) is the real source and was tested against oracles: 1,071,414 checks passed under AddressSanitizer and UBSan, comparing each part with the C library or with a plain reference (the allocation-failure test makes `realloc` fail at chosen calls). I tried 19 deliberate bugs in it (an `align_up` that adds `a` instead of `a - 1`, a vector that overwrites its only pointer with the result of a failing `realloc`, a heapsort that starts the heap one level too low, a state table with a wrong destination, and others); all 19 were caught, 17 by a failed check and 2 by the sanitizers. I also compiled the declaration drills in section 1 so that the compiler itself checks each English answer. Sanitizer output is shortened: the process id, addresses, and frames from the C library and the sanitizer are removed, and the rest is verbatim. Statements about ARM64, Clang, musl, Windows and exact glibc internals are from memory and marked.

**Data and timing.** Machine: a cloud container, Intel Xeon 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Timings are the best of 3 wall-clock runs in milliseconds, shown as run 1 / run 2 (two complete repetitions of the whole benchmark). They show the shape of the cost on this machine and will differ on yours.

---

## 1. Reading declarations

A C declaration is written so that **the declaration mimics the use**: `int *a[5]` says that `*a[i]` is an `int`. That is why it reads inside out. The procedure that always works:

1. **Find the identifier** (the name being declared). Start there.
2. **Go right** as long as you see postfix operators: `[N]` means "array of N", `(...)` means "function taking ... returning". Stop at a closing parenthesis or the end.
3. **Go left** past prefix operators: `*` means "pointer to", `const` and `volatile` qualify what is directly on their left (or on their right if nothing is to their left).
4. When you reach a parenthesis, step out of it and **continue right from it**.
5. What remains at the end is the base type.

The fact behind steps 2 and 3: **postfix operators bind tighter than `*`**. So `int *a[5]` is an array of pointers (the `[5]` attaches to `a` first), and you need parentheses to get a pointer to an array: `int (*a)[5]`. The spiral rule taught in some books is the same procedure drawn differently. The most reliable method is the third one, **build it from typedefs**: name each layer, which also makes the code better.

The 20 drills below each pair the declaration with the same type built layer by layer from typedefs. The line after each declaration is the English reading. The compiler checks that the two types are identical (`_Static_assert` with `__builtin_types_compatible_p`), so a wrong English reading would fail to compile. **Do them with the comments covered.**

```c
#include <stddef.h>

/* Each drill: the declaration (d..), then the same type built step by step
   from typedefs (a..). The compiler checks that the two are the same type. */
#define SAME(d, a) _Static_assert(__builtin_types_compatible_p(__typeof__(d), __typeof__(a)), "drill mismatch")

int *d01[5];                                   /* array of 5 pointers to int */
typedef int *T01[5]; T01 a01; SAME(d01, a01);

int (*d02)[5];                                 /* pointer to array of 5 int */
typedef int Arr02[5]; Arr02 *a02; SAME(d02, a02);

int *d03(void);                                /* function returning pointer to int */
typedef int *Fn03(void); Fn03 a03; SAME(d03, a03);

int (*d04)(void);                              /* pointer to function returning int */
typedef int Fn04(void); Fn04 *a04; SAME(d04, a04);

int (*d05[4])(int);                            /* array of 4 pointers to function (int) returning int */
typedef int Fn05(int); typedef Fn05 *P05; P05 a05[4]; SAME(d05, a05);

int *(*d06)(int);                              /* pointer to function (int) returning pointer to int */
typedef int *Fn06(int); Fn06 *a06; SAME(d06, a06);

int (*(*d07)(void))[3];                        /* pointer to function (void) returning pointer to array of 3 int */
typedef int Arr07[3]; typedef Arr07 *Ret07; typedef Ret07 Fn07(void); Fn07 *a07; SAME(d07, a07);

char *(*(*d08)(void))(int);                    /* pointer to function (void) returning pointer to function (int) returning pointer to char */
typedef char *Fn08b(int); typedef Fn08b *Ret08; typedef Ret08 Fn08(void); Fn08 *a08; SAME(d08, a08);

void (*d09(int, void (*)(int)))(int);          /* function (int, pointer to function (int) returning void) returning pointer to function (int) returning void: the declaration of signal */
typedef void Fn09(int); typedef Fn09 *H09; H09 a09(int, H09); SAME(d09, a09);

int *const d10 = NULL;                         /* constant pointer to int */
typedef int *CP10; const CP10 a10 = NULL; SAME(d10, a10);

const int *d11;                                /* pointer to constant int */
typedef const int CI11; CI11 *a11; SAME(d11, a11);

int *const *d12;                               /* pointer to constant pointer to int */
typedef int *const CP12; CP12 *a12; SAME(d12, a12);

const char *const d13[3] = {0};                /* array of 3 constant pointers to constant char */
typedef const char CC13; typedef CC13 *const CP13; CP13 a13[3] = {0}; SAME(d13, a13);

char (*(*d14[3])(void))[5];                    /* array of 3 pointers to function (void) returning pointer to array of 5 char */
typedef char Arr14[5]; typedef Arr14 *Ret14; typedef Ret14 Fn14(void); Fn14 *a14[3]; SAME(d14, a14);

int (**d15)(void);                             /* pointer to pointer to function (void) returning int */
typedef int Fn15(void); Fn15 **a15; SAME(d15, a15);

void (*d16)(void *, int);                      /* pointer to function (pointer to void, int) returning void: a callback */
typedef void Fn16(void *, int); Fn16 *a16; SAME(d16, a16);

int (*d17)(const void *, const void *);       /* pointer to function (two pointers to const void) returning int: the qsort comparator */
typedef int Fn17(const void *, const void *); Fn17 *a17; SAME(d17, a17);

int (*d18[2][3])(int);                         /* 2 by 3 array of pointers to function (int) returning int */
typedef int Fn18(int); typedef Fn18 *P18; typedef P18 Row18[3]; Row18 a18[2]; SAME(d18, a18);

size_t (*const d19[2])(const char *) = {0};    /* array of 2 constant pointers to function (pointer to const char) returning size_t */
typedef size_t Fn19(const char *); typedef Fn19 *const CP19; CP19 a19[2] = {0}; SAME(d19, a19);

volatile int *const d20 = NULL;                /* constant pointer to volatile int: a hardware register address */
typedef volatile int V20; typedef V20 *const CP20; CP20 a20 = NULL; SAME(d20, a20);

int main(void) { return 0; }
```

```text
all 20 drills compile: every declaration has the type its English answer describes
```

If I had read drill 1 as "pointer to array of 5 pointers" the build would have stopped. This is the real error for a wrong guess (`int (*e01)[5]` compared with an array of 5 pointers to `int`):

```text
drill_bad.c:1:20: error: static assertion failed: "drill mismatch"
```

Points to remember from the drills:

- **`const` position.** `const int *p` and `int const *p` are the same (pointer to constant int). `int *const p` is a constant pointer. Read the `const` as applying to the thing on its left. A hardware register address is `volatile uint32_t *const reg` (drill 20): the pointer never changes, the pointee must not be cached.
- **`signal`** (drill 9) is the standard example of why typedefs exist: with `typedef void (*Handler)(int);` it becomes `Handler signal(int, Handler);`.
- **Parameters adjust.** In a parameter list `int a[]`, `int a[10]` and `int *a` are all `int *`, and `int f(void)` against `int f()` differ: an empty parameter list means "unspecified" before C23 (C 02). Write `(void)`.
- **Tools.** The `cdecl` program and the website `cdecl.org` translate both ways (from memory; I did not run it here). In an interview, write the typedef chain on the board.

## 2. `*p++`, `(*p)++`, `*++p`, `++*p`

Postfix `++` binds tighter than unary `*`, and both prefix operators (`*`, `++`) group right to left. So:

| Expression | Parsed as | Meaning |
|---|---|---|
| `*p++` | `*(p++)` | value of `*p`, then advance the pointer |
| `(*p)++` | `(*p)++` | value of `*p`, then increment the pointee |
| `*++p` | `*(++p)` | advance the pointer, then the new `*p` |
| `++*p` | `++(*p)` | increment the pointee, value is the new one |

```c
#include <stdio.h>

static void show(const char *expr, int v, const int *a, const int *p)
{
    printf("%-9s value %2d   a = {%d, %d, %d, %d}   p points at a[%td]\n", expr, v, a[0], a[1], a[2], a[3], p - a);
}

int main(void)
{
    int a[4] = {10, 20, 30, 40};
    int *p = a;
    int v;

    v = *p++;
    show("*p++", v, a, p);
    a[0] = 10; p = a;
    v = (*p)++;
    show("(*p)++", v, a, p);
    a[0] = 10; p = a;
    v = *++p;
    show("*++p", v, a, p);
    a[0] = 10; p = a;
    v = ++*p;
    show("++*p", v, a, p);

    char src[] = "copy", dst[8], *s = src, *d = dst;
    while ((*d++ = *s++))
        ;
    printf("while ((*d++ = *s++)) copied \"%s\" and left d pointing %td bytes into dst\n", dst, d - dst);
    return 0;
}
```

```text
*p++      value 10   a = {10, 20, 30, 40}   p points at a[1]
(*p)++    value 10   a = {11, 20, 30, 40}   p points at a[0]
*++p      value 20   a = {10, 20, 30, 40}   p points at a[1]
++*p      value 11   a = {11, 20, 30, 40}   p points at a[0]
while ((*d++ = *s++)) copied "copy" and left d pointing 5 bytes into dst
```

`*p++` returned 10 and moved `p` to `a[1]` without changing the array. `(*p)++` returned 10, changed `a[0]` to 11 and left `p` alone. `*++p` moved first and read 20. `++*p` changed `a[0]` and returned 11. The last line is the classic copy loop: `*d++ = *s++` copies a byte and advances both pointers, and the assignment's value (the copied byte) ends the loop when it is the terminator. After copying `copy` (4 characters plus the terminator) `d` points 5 bytes in. It is an idiom to read fluently, not to write in new code without a bound (C 07). Never use two of these on the same object in one expression (`*p++ = *p`), because the order of evaluation is unspecified and the behaviour is undefined.

## 3. The pointer bug catalogue

Every bug below is a program of its own. The tools:

| Tool | Flag | Finds | Cost |
|---|---|---|---|
| AddressSanitizer (ASan) | `-fsanitize=address` | out-of-bounds, use after free, double free, bad free, leaks (LeakSanitizer is part of it on Linux) | about 2 times slower, more memory |
| UndefinedBehaviorSanitizer | `-fsanitize=undefined` | misaligned access, overflow, bad shifts, null pointer use | small |
| valgrind (Memcheck) | none, run `valgrind ./prog` | uninitialised reads, invalid reads and writes, leaks | 10 to 50 times slower (from memory) |
| The compiler | `-Wall -Wextra` | some of the above, statically | free |
| glibc itself | none | double free and invalid free, with an abort | free |

Start with the compiler warnings, then ASan, then valgrind for uninitialised values, which ASan does not detect.

### 3.1 Null pointer

```c
int main(void)
{
    int *p = 0;
    *p = 1;
    return 0;
}
```

```text
ERROR: AddressSanitizer: SEGV on unknown address ADDR (pc ADDR bp ADDR sp ADDR T0)
    #0 main b01_null.c:4
    #1 __libc_start_call_main libc_start_call_main.h:58
    #2 __libc_start_main_impl ../csu/libc-start.c:360
SUMMARY: AddressSanitizer: SEGV b01_null.c:4 in main
```

ASan reports "SEGV on unknown address" at address 0 and the line. Without it you get `Segmentation fault`, exit status 139. Null checks belong at the places where a pointer enters your code (arguments, return values of `malloc`, `fopen`, `strchr`, `getenv`). Dereferencing a null pointer is undefined, not guaranteed to crash: on some embedded targets address 0 is valid memory, and an optimiser may delete a later null check (C 01).

### 3.2 Uninitialised pointer

A local pointer that is never assigned holds whatever was in that stack slot. Using it is undefined. The compiler warns and valgrind names it:

```c
#include <stdio.h>

int main(void)
{
    int *p;
    if (p)
        puts("non-null");
    else
        puts("null");
    return 0;
}
```

```text
b02_uninit.c:6:8: warning: 'p' is used uninitialized [-Wuninitialized]
Conditional jump or move depends on uninitialised value(s)
at ADDR: main (6)
```

valgrind reports a conditional jump that depends on an uninitialised value at line 6. ASan would not catch this one (it tracks addresses, not whether a value was ever written). The habit that prevents it: initialise pointers where you declare them (`= NULL`), and declare them at first use.

### 3.3 Dangling pointer to a local

```c
#include <stdio.h>

static int *make(void)
{
    int local = 42;
    return &local;
}

int main(void)
{
    int *p = make();
    printf("%d\n", *p);
    return 0;
}
```

```text
b03_dangling.c:6:12: warning: function returns address of local variable [-Wreturn-local-addr]
ERROR: AddressSanitizer: SEGV on unknown address ADDR (pc ADDR bp ADDR sp ADDR T0)
    #0 main b03_dangling.c:12
    #1 __libc_start_call_main libc_start_call_main.h:58
    #2 __libc_start_main_impl ../csu/libc-start.c:360
SUMMARY: AddressSanitizer: SEGV b03_dangling.c:12 in main
```

The compiler warned that the function returns the address of a local variable. When I ran it, I expected garbage or a stack-use-after-return report. What I got was a **null pointer dereference**: GCC saw that returning the address of a local is undefined and, in this build, made `make` return NULL (my inference from the null address in the report; I did not read the assembly). That is the point of undefined behaviour: the optimiser is allowed to assume it does not happen, and the symptom stops resembling the cause. The usual real-world form is subtler: a pointer to a local stored in a struct or a global, used after the function returned. The fixes are to return a value, to take a buffer from the caller, or to allocate on the heap and document the ownership.

### 3.4 Use after free

```c
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    int *p = malloc(4 * sizeof *p);
    p[0] = 7;
    free(p);
    printf("%d\n", p[0]);
    return 0;
}
```

```text
ERROR: AddressSanitizer: heap-use-after-free on address ADDR at pc ADDR bp ADDR sp ADDR
READ of size 4 at ADDR thread T0
    #0 main b04_uaf.c:9
    #1 __libc_start_call_main libc_start_call_main.h:58
    #2 __libc_start_main_impl ../csu/libc-start.c:360
ADDR is located 0 bytes inside of 16-byte region [ADDR,ADDR)
freed by thread T0 here:
    #1 main b04_uaf.c:8
    #2 __libc_start_call_main libc_start_call_main.h:58
    #3 __libc_start_main_impl ../csu/libc-start.c:360
previously allocated by thread T0 here:
    #1 main b04_uaf.c:6
    #2 __libc_start_call_main libc_start_call_main.h:58
    #3 __libc_start_main_impl ../csu/libc-start.c:360
SUMMARY: AddressSanitizer: heap-use-after-free b04_uaf.c:9 in main
```

ASan gives three stories in one report: the bad read (line 9), where the block was freed (line 8), and where it was allocated (line 6). That is why it is the first tool to try. Without ASan this program usually "works": the freed block is still mapped and the old value is still there. It fails later, in a different place, when the allocator reuses the block for something else. Fix: set the pointer to `NULL` after `free` (section 4), and do not keep a second copy of a pointer whose lifetime ends.

### 3.5 Double free

```c
#include <stdlib.h>

int main(void)
{
    char *p = malloc(16);
    free(p);
    free(p);
    return 0;
}
```

```text
ERROR: AddressSanitizer: attempting double-free on thread T0:
    #1 main b05_double_free.c:7
    #2 __libc_start_call_main libc_start_call_main.h:58
    #3 __libc_start_main_impl ../csu/libc-start.c:360
ADDR is located 0 bytes inside of 16-byte region [ADDR,ADDR)
freed by thread T0 here:
    #1 main b05_double_free.c:6
    #2 __libc_start_call_main libc_start_call_main.h:58
    #3 __libc_start_main_impl ../csu/libc-start.c:360
previously allocated by thread T0 here:
    #1 main b05_double_free.c:5
    #2 __libc_start_call_main libc_start_call_main.h:58
    #3 __libc_start_main_impl ../csu/libc-start.c:360
SUMMARY: AddressSanitizer: double-free asan_malloc_linux.cpp:52 in free
```

This is what glibc says on its own, with no sanitizer:

```text
$ ./b05_double_free   (no sanitizer)
free(): double free detected in tcache 2
exit status 134
$ ./b08_free_stack   (no sanitizer)
exit status 139
$ ./b09_free_interior   (no sanitizer)
free(): invalid pointer
exit status 134
```

glibc aborted with `free(): double free detected in tcache 2` (exit status 134, which is 128 plus `SIGABRT`). The check is cheap and not guaranteed: a double free that is not caught corrupts the free lists and gives an attacker control of later allocations (C 11). It usually comes from two owners of one pointer (an alias, or an error path that frees what the normal path also frees). The same listing shows two other bad frees, covered below: a stack address (exit status 139, a plain crash, with no message), and a pointer into the middle of a block (`free(): invalid pointer`).

### 3.6 A stale pointer after `realloc`

`realloc` may move the block. Every pointer into the old block, and the old pointer itself, is then invalid:

```c
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    int *a = malloc(4 * sizeof *a);
    int *first = &a[0];
    a[0] = 1;
    a = realloc(a, 1 << 20);
    a[0] = 2;
    printf("%d\n", *first);
    free(a);
    return 0;
}
```

```text
ERROR: AddressSanitizer: heap-use-after-free on address ADDR at pc ADDR bp ADDR sp ADDR
READ of size 4 at ADDR thread T0
    #0 main b06_realloc.c:11
    #1 __libc_start_call_main libc_start_call_main.h:58
    #2 __libc_start_main_impl ../csu/libc-start.c:360
ADDR is located 0 bytes inside of 16-byte region [ADDR,ADDR)
freed by thread T0 here:
    #1 main b06_realloc.c:9
    #2 __libc_start_call_main libc_start_call_main.h:58
    #3 __libc_start_main_impl ../csu/libc-start.c:360
previously allocated by thread T0 here:
    #1 main b06_realloc.c:6
    #2 __libc_start_call_main libc_start_call_main.h:58
    #3 __libc_start_main_impl ../csu/libc-start.c:360
SUMMARY: AddressSanitizer: heap-use-after-free b06_realloc.c:11 in main
```

The pointer `first` was taken before the call and used after it. ASan always moves the block on `realloc`, so it catches this every time; the normal allocator moves it only when it cannot grow in place, so the bug can hide for months. The dynamic-array version of this bug is the most common one in practice: keep an **index**, not a pointer, to an element of a vector that can grow (section 4).

### 3.7 Leak

```c
#include <stdlib.h>
#include <string.h>

static void work(void)
{
    char *buf = malloc(32);
    strcpy(buf, "never freed");
}

int main(void)
{
    work();
    return 0;
}
```

```text
ERROR: LeakSanitizer: detected memory leaks
Direct leak of 32 byte(s) in 1 object(s) allocated from:
    #1 work b07_leak.c:6
    #2 main b07_leak.c:12
    #3 __libc_start_call_main libc_start_call_main.h:58
    #4 __libc_start_main_impl ../csu/libc-start.c:360
SUMMARY: AddressSanitizer: 32 byte(s) leaked in 1 allocation(s).
```

LeakSanitizer reports the allocation site (`work`, line 6, called from `main`) and the size. A leak does not crash anything, so it is found by tools or in production by memory growth. The usual causes are an early `return` that skips the cleanup (the next section's `goto cleanup` idiom, C 10), overwriting the only pointer (`p = malloc(...)` twice), and `p = realloc(p, n)` when `realloc` fails.

### 3.8 Freeing something that is not a heap block

```c
#include <stdlib.h>

int main(void)
{
    int local[4] = {0};
    free(local);
    return 0;
}
```

```text
ERROR: AddressSanitizer: attempting free on address which was not malloc()-ed: thread T0
    #1 main b08_free_stack.c:6
    #2 __libc_start_call_main libc_start_call_main.h:58
    #3 __libc_start_main_impl ../csu/libc-start.c:360
Address ADDR is located in stack of thread T0 at offset 32 in frame
    #0 main b08_free_stack.c:4
SUMMARY: AddressSanitizer: bad-free asan_malloc_linux.cpp:52 in free
```

```c
#include <stdlib.h>

int main(void)
{
    char *p = malloc(32);
    p += 8;
    free(p);
    return 0;
}
```

```text
ERROR: AddressSanitizer: attempting double-free on thread T0:
    #1 main b09_free_interior.c:7
    #2 __libc_start_call_main libc_start_call_main.h:58
    #3 __libc_start_main_impl ../csu/libc-start.c:360
ADDR is located 8 bytes inside of 32-byte region [ADDR,ADDR)
    #1 main b09_free_interior.c:5
    #2 __libc_start_call_main libc_start_call_main.h:58
    #3 __libc_start_main_impl ../csu/libc-start.c:360
SUMMARY: AddressSanitizer: double-free asan_malloc_linux.cpp:52 in free
```

A pointer given to `free` must be exactly one that `malloc`, `calloc` or `realloc` returned. ASan names the first case plainly (an address in a stack frame). For the second, it reported `attempting double-free` although nothing had been freed before: its allocator wording is not precise for this case, and the line that tells the truth is "8 bytes inside of 32-byte region", which says that the pointer points into a block and not at its start. glibc says `free(): invalid pointer` for it (above). The usual cause is advancing a pointer through a buffer and then freeing the moving pointer: keep the original pointer for the free.

### 3.9 Heap buffer overflow: the missing terminator byte

```c
#include <stdlib.h>
#include <string.h>

int main(void)
{
    const char *name = "abcdefgh";
    char *copy = malloc(strlen(name));
    strcpy(copy, name);
    free(copy);
    return 0;
}
```

```text
ERROR: AddressSanitizer: heap-buffer-overflow on address ADDR at pc ADDR bp ADDR sp ADDR
WRITE of size 9 at ADDR thread T0
    #1 main b10_overflow.c:8
    #2 __libc_start_call_main libc_start_call_main.h:58
    #3 __libc_start_main_impl ../csu/libc-start.c:360
ADDR is located 0 bytes after 8-byte region [ADDR,ADDR)
    #1 main b10_overflow.c:7
    #2 __libc_start_call_main libc_start_call_main.h:58
    #3 __libc_start_main_impl ../csu/libc-start.c:360
SUMMARY: AddressSanitizer: heap-buffer-overflow asan_interceptors.cpp:563 in strcpy
```

`malloc(strlen(name))` allocates 8 bytes for a string that needs 9. ASan reports a write of size 9 that begins at the allocation and ends one byte past it ("0 bytes after 8-byte region"). It is the commonest off-by-one in C. The fix is `strlen(name) + 1`, or `strdup`.

### 3.10 A callee cannot clear the caller's pointer

C passes pointers **by value**. A function that frees its argument has a private copy of the pointer, so setting it to `NULL` changes nothing for the caller, who is left with a dangling pointer:

```c
#include <stdio.h>
#include <stdlib.h>
#include "../code/ptr.h"

static void drop_wrong(int *p) { free(p); p = NULL; }
static void drop_right(int **pp) { free(*pp); *pp = NULL; }

int main(void)
{
    int *a = malloc(sizeof *a);
    drop_wrong(a);
    printf("after drop_wrong: a is %s (the callee cleared only its own copy)\n", a ? "still non-NULL" : "NULL");
    a = malloc(sizeof *a);
    drop_right(&a);
    printf("after drop_right: a is %s\n", a ? "still non-NULL" : "NULL");
    int *b = malloc(sizeof *b);
    FREE(b);
    printf("after FREE(b):    b is %s\n", b ? "still non-NULL" : "NULL");
    return 0;
}
```

```text
after drop_wrong: a is still non-NULL (the callee cleared only its own copy)
after drop_right: a is NULL
after FREE(b):    b is NULL
```

`drop_wrong` cleared its copy and the caller's `a` stayed non-NULL. `drop_right` takes a `T **` (the address of the caller's pointer) and clears the original. The macro `FREE(b)` does the same by assignment. This is the `release` function and the macro that the tests check; section 4 prints them. The `void **` form needs a cast at the call site (`release((void **)&a)`), which is strictly not allowed by the aliasing rules, so a macro is usually the better tool.

### 3.11 `malloc(0)`

```c
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    void *p = malloc(0);
    printf("malloc(0) returned %s, usable size %zu\n", p ? "a non-NULL pointer" : "NULL", p ? malloc_usable_size(p) : 0);
    void *q = realloc(p, 0);
    printf("realloc(p, 0) returned %s (glibc frees the block; C23 calls this undefined)\n", q ? "non-NULL" : "NULL");
    return 0;
}
```

```text
malloc(0) returned a non-NULL pointer, usable size 24
realloc(p, 0) returned NULL (glibc frees the block; C23 calls this undefined)
```

The standard allows `malloc(0)` to return NULL or a unique pointer that must not be dereferenced, and glibc does the second (with a 24 byte block). That means `if (!malloc(n)) fail` is wrong for `n == 0` on a system that returns NULL, and `realloc(p, 0)` frees the block and returns NULL on glibc, but is undefined in C23. Never rely on either: check `n` yourself.

### 3.12 Aliasing: two pointers of different types

The compiler assumes that a store through an `int *` cannot change a `float` object, and the other way round (the **strict aliasing** rule, C 05). If you cast a pointer to defeat the types, it may optimise on an assumption that is false:

```c
#include <stdio.h>

__attribute__((noinline)) static int pun(int *i, float *f)
{
    *i = 1;
    *f = 2.0f;
    return *i;
}

int main(void)
{
    int x = 0;
    int r = pun(&x, (float *)&x);
    printf("pun returned %d\n", r);
    return 0;
}
```

```text
b13_alias.c:13:30: warning: dereferencing type-punned pointer will break strict-aliasing rules [-Wstrict-aliasing]
-O0:                          pun returned 1073741824
-O2:                          pun returned 1
-O2 -fno-strict-aliasing:     pun returned 1073741824
```

At `-O0` the function stored the float 2.0 over the int and returned its bit pattern, 1073741824 (`0x40000000`). At `-O2` it returned 1: it assumed the store through `f` cannot change `*i`, and returned the value it had just stored. The same program is correct only with `-fno-strict-aliasing` (third line), which switches the assumption off. The same source gives two answers depending on the optimisation level, which is the signature of undefined behaviour. The legal ways to look at the bytes of an object are `memcpy` into an object of the other type, a `union` (C 04) or a `char *` access.

### 3.13 Misaligned access

A pointer to an `int` that is not 4-byte aligned is undefined to dereference. x86-64 tolerates it for ordinary loads; some other targets trap (ARM cores without unaligned support, and many vector instructions, from memory), and the compiler may assume alignment (it can emit an instruction that requires it). The UBSan report, `load of misaligned address`, is shown in C 04, section 6; the portable way to read a value from a byte buffer is `memcpy` into a local or shifts (C 07, section 8.3).

### 3.14 `SIGBUS` against `SIGSEGV`

A **segmentation fault** (`SIGSEGV`) is an access to an address that has no valid mapping or the wrong permission. A **bus error** (`SIGBUS`) is an access to an address that is mapped but the hardware or the kernel cannot supply the memory behind it. The Linux example is a memory-mapped file that was truncated: the page is still in the address space, but there is nothing in the file for it any more.

```c
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

int main(void)
{
    int fd = open("bus.tmp", O_RDWR | O_CREAT | O_TRUNC, 0600);
    ftruncate(fd, 4096);
    volatile char *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    m[0] = 'x';
    printf("mapped file page works: %c\n", m[0]);
    fflush(stdout);
    ftruncate(fd, 0);
    printf("read after the file was truncated: %c\n", m[0]);
    return 0;
}
```

```text
mapped file page works: x
Bus error               ./b14
exit status 135
```

The first access worked; after `ftruncate(fd, 0)` the same read killed the process with `Bus error` (exit status 135, which is 128 plus signal 7). On some older and embedded architectures a misaligned access also raises `SIGBUS` (from memory), which is the source of the name "bus error" in many interview questions. If you read a mapped file that another process can shrink, you need a signal handler or you need to copy the data.

### 3.15 Stack overflow: deep recursion and big variable-length arrays

The stack has a fixed limit (`ulimit -s`, 8 MiB here, C 03). Recursion without a base case, or a very large local array, runs off the end. This program recurses until it dies, catching the signal on an alternate stack to report how far it got:

```c
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static long depth;
static char *first, *second;

static void on_segv(int sig)
{
    char msg[160];
    (void)sig;
    int n = snprintf(msg, sizeof msg, "frame size %ld bytes; about %ld frames before the stack ran out\n",
                     (long)(first - second), depth / 1000 * 1000);
    write(1, msg, (size_t)n);
    _exit(0);
}

__attribute__((noinline)) static long recurse(long n)
{
    char pad[1];
    pad[0] = (char)n;
    if (n == 1) first = pad;
    if (n == 2) second = pad;
    depth = n;
    return recurse(n + 1) + pad[0];
}

int main(void)
{
    static char alt[65536];
    stack_t ss = { .ss_sp = alt, .ss_size = sizeof alt };
    sigaltstack(&ss, NULL);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_segv;
    sa.sa_flags = SA_ONSTACK;
    sigaction(SIGSEGV, &sa, NULL);
    recurse(1);
    return 0;
}
```

```text
frame size 48 bytes; about 173000 frames before the stack ran out
frame size 48 bytes; about 173000 frames before the stack ran out
```

Each frame used 48 bytes, and about 173,000 frames fit in 8 MiB (the exact depth varies by a few hundred between runs, so I print it to the nearest thousand). The compiler's warning is correct:

```text
b15_stack.c:21:39: warning: infinite recursion detected [-Winfinite-recursion]
```

A recursive algorithm over untrusted input (a parser for nested brackets, a tree walk on a list-shaped tree) can be made to do this on purpose: bound the depth or use an explicit stack. A variable-length array puts its size on the caller's stack:

```c
#include <stdio.h>
#include <stdlib.h>

static int fill(int n)
{
    int a[n];
    for (int i = 0; i < n; ++i)
        a[i] = i;
    return a[n - 1];
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 1000;
    printf("int a[%d] (%ld bytes) -> last element %d\n", n, (long)n * 4, fill(n));
    return 0;
}
```

```text
b16_vla.c:6:5: warning: ISO C90 forbids variable length array 'a' [-Wvla]
int a[1000] (4000 bytes) -> last element 999
Segmentation fault      ./b16 4000000
exit status 139
```

A 4,000 byte array was fine; a 16 MB one crashed with exit status 139, and there is no error to catch, because nothing checks the size of a VLA. A VLA whose size comes from input is a stack overflow that the attacker controls. Use `malloc`, or a fixed bound with a check (C 10 comes back to this).

## 4. Writing the safe versions

Two pieces of code carry most of the lessons of section 3, and the test suite checks both. The first is the `FREE` macro and `release`. The second is a growable array that survives allocation failure.

```c
#define FREE(p) do { free(p); (p) = NULL; } while (0)

/* Frees *pp and clears the caller's pointer: the T ** fix for a callee that frees. */
static void release(void **pp)
{
    free(*pp);
    *pp = NULL;
}
```

```c
typedef struct {
    int *data;
    size_t len, cap;
} Vec;

/* The allocator is a variable so a test can make it fail. */
static void *(*vec_realloc)(void *, size_t) = realloc;

/* Append x. Returns 0, or -1 with the vector unchanged and still usable. */
static int vec_push(Vec *v, int x)
{
    if (v->len == v->cap) {
        size_t ncap = v->cap ? v->cap * 2 : 4;
        int *p;
        if (v->cap > SIZE_MAX / 2 / sizeof *p)
            return -1;
        p = vec_realloc(v->data, ncap * sizeof *p);
        if (!p)
            return -1;
        v->data = p;
        v->cap = ncap;
    }
    v->data[v->len++] = x;
    return 0;
}

static void vec_free(Vec *v)
{
    free(v->data);
    v->data = NULL;
    v->len = v->cap = 0;
}
```

What each line is for:

- **`p = vec_realloc(v->data, ...); if (!p) return -1; v->data = p;`** The result of `realloc` goes into a temporary. If `realloc` fails, it returns NULL and **leaves the old block allocated**; writing `v->data = realloc(v->data, n)` would overwrite the only pointer to the old block with NULL and leak it. A deliberate bug that does exactly that was caught by the failure-injection test (the vector lost its data).
- **Doubling** gives amortised O(1) per push: each element is copied at most about twice over its life.
- **The overflow check** `v->cap > SIZE_MAX / 2 / sizeof *p` stops `cap * 2 * sizeof` from wrapping to a small number, the integer-overflow route to a heap overflow (C 11). A mutant that removed it was caught by the sanitizer.
- **`vec_free`** clears all three fields, so a freed vector looks empty and a second `vec_free` is harmless.
- **The allocator is a pointer** (`vec_realloc`) only so the test can make it fail at the 1st, 2nd, ... call; the test then checks that the vector is unchanged and still usable after every failure. This is how to test error paths that never happen in normal runs.

And this is what the stale-pointer rule looks like with it: a pointer to an element, kept across pushes.

```c
#include <stdio.h>
#include "../code/ptr.h"

int main(void)
{
    Vec v = {0};
    vec_push(&v, 1);
    int *first = &v.data[0];
    for (int i = 0; i < 100; ++i)
        vec_push(&v, i);
    printf("%d\n", *first);
    vec_free(&v);
    return 0;
}
```

```text
ERROR: AddressSanitizer: heap-use-after-free on address ADDR at pc ADDR bp ADDR sp ADDR
READ of size 4 at ADDR thread T0
    #0 main p_vec.c:11
    #1 __libc_start_call_main libc_start_call_main.h:58
    #2 __libc_start_main_impl ../csu/libc-start.c:360
ADDR is located 0 bytes inside of 16-byte region [ADDR,ADDR)
freed by thread T0 here:
    #1 vec_push ptr.h:57
    #2 main p_vec.c:10
    #3 __libc_start_call_main libc_start_call_main.h:58
    #4 __libc_start_main_impl ../csu/libc-start.c:360
previously allocated by thread T0 here:
    #1 vec_push ptr.h:57
    #2 main p_vec.c:7
    #3 __libc_start_call_main libc_start_call_main.h:58
    #4 __libc_start_main_impl ../csu/libc-start.c:360
SUMMARY: AddressSanitizer: heap-use-after-free p_vec.c:11 in main
```

`first` was valid when it was taken and invalid after the pushes moved the storage; ASan shows `vec_push` freeing the old block (line 57 of the header) and the read in `main`. **The rule for any growable container: an element pointer is valid only until the next operation that can grow it.** Hold an index and re-derive the pointer. The C++ standard library has the same rule for `std::vector` iterators (from memory).

## 5. `restrict` in practice

`restrict` promises the compiler that, for the life of the pointer, the object it points at is accessed only through that pointer (or through pointers derived from it). The standard library uses it in signatures that forbid overlap: `memcpy(void *restrict, const void *restrict, size_t)`, against `memmove`, which has no `restrict`, which is the formal statement of the rule from C 07. Two rules: (1) breaking the promise (passing overlapping buffers to a function whose parameters are `restrict`) is undefined behaviour, and (2) the benefit is that the compiler may keep values in registers and reorder or vectorise stores. C 05 measured it and found **no speed difference** on the loops I tried; the gain is real only when aliasing is what prevents an optimisation, which you confirm by reading the assembly, not by adding the keyword. Use it where the contract genuinely holds (copy and filter functions with distinct input and output), and document it.

## 6. Pointers as integers

`uintptr_t` (from `<stdint.h>`) is an unsigned integer type that can hold a pointer converted to it and convert back to the same pointer (optional in the standard, present on every platform you will use). Converting is for **arithmetic on the address, not for storing pointers in integers by habit**. Subtraction and comparison of two pointers are defined only for pointers into the same array (or one past its end); comparing unrelated pointers with `<` is unspecified, and comparing through `uintptr_t` is the usual way out (the `xmemmove` of C 07 does it).

### 6.1 Alignment arithmetic

For a power-of-two alignment `a`: round down with `x & ~(a - 1)`, round up with `(x + a - 1) & ~(a - 1)`, test with `(x & (a - 1)) == 0`. Allocators, page-aligned buffers, DMA descriptors and ring buffers all do this:

```c
/* a must be a power of two. */
static int is_aligned(const void *p, size_t a) { return ((uintptr_t)p & (a - 1)) == 0; }
static uintptr_t align_down(uintptr_t x, size_t a) { return x & ~(uintptr_t)(a - 1); }
static uintptr_t align_up(uintptr_t x, size_t a) { return (x + a - 1) & ~(uintptr_t)(a - 1); }

/* Tagged pointer: the low 3 bits of an 8-byte aligned pointer are free. */
#define TAG_BITS 3u
#define TAG_MASK ((uintptr_t)((1u << TAG_BITS) - 1))

static int tag_make(const void *p, unsigned tag, uintptr_t *out)
{
    if (((uintptr_t)p & TAG_MASK) != 0 || tag > TAG_MASK)
        return -1;
    *out = (uintptr_t)p | tag;
    return 0;
}
static void *tag_ptr(uintptr_t t) { return (void *)(t & ~TAG_MASK); }
static unsigned tag_val(uintptr_t t) { return (unsigned)(t & TAG_MASK); }
```

### 6.2 Tagged pointers

An object aligned to 8 bytes has an address whose low 3 bits are zero. Those bits can carry a small tag (a type, a colour, a flag) at no cost in space, as long as you strip them before dereferencing. The listing above (second half) is a tested version: `tag_make` rejects a misaligned pointer and a tag larger than 7 (so it cannot silently corrupt an address), `tag_ptr` and `tag_val` take it apart. Running it on a real `malloc` result:

```c
#include <stdio.h>
#include <stdlib.h>
#include "../code/ptr.h"

typedef struct Node {
    struct Node *next;
    int value;
} Node;

enum { K_INT = 1, K_STR = 5 };

int main(void)
{
    Node *n = malloc(sizeof *n);
    n->value = 99;
    n->next = NULL;
    printf("malloc returned an address that is 16-byte aligned: %s\n", is_aligned(n, 16) ? "yes" : "no");
    uintptr_t t;
    int rc = tag_make(n, K_STR, &t);
    printf("tag_make rc=%d, tag read back=%u, pointer read back is the same node: %s\n", rc, tag_val(t), tag_ptr(t) == n ? "yes" : "no");
    printf("value through the recovered pointer: %d\n", ((Node *)tag_ptr(t))->value);
    char *odd = (char *)n + 1;
    printf("tag_make on an odd address: rc=%d\n", tag_make(odd, K_INT, &t));
    printf("tag_make with tag 9: rc=%d\n", tag_make(n, 9, &t));
    printf("align_up(13, 8)=%lu  align_down(13, 8)=%lu  align_up(4096, 4096)=%lu  align_up(4097, 4096)=%lu\n",
           (unsigned long)align_up(13, 8), (unsigned long)align_down(13, 8), (unsigned long)align_up(4096, 4096), (unsigned long)align_up(4097, 4096));
    printf("sizeof(Node)=%zu; the low bits of a Node address are free only because Node is 8-byte aligned\n", sizeof(Node));
    free(n);
    return 0;
}
```

```text
malloc returned an address that is 16-byte aligned: yes
tag_make rc=0, tag read back=5, pointer read back is the same node: yes
value through the recovered pointer: 99
tag_make on an odd address: rc=-1
tag_make with tag 9: rc=-1
align_up(13, 8)=16  align_down(13, 8)=8  align_up(4096, 4096)=4096  align_up(4097, 4096)=8192
sizeof(Node)=16; the low bits of a Node address are free only because Node is 8-byte aligned
```

`malloc` returned a 16-byte aligned block, so 4 low bits would be free; the code uses 3 because the type it relies on, `Node`, is only guaranteed 8-byte aligned by its contents. The tag survived the round trip and the recovered pointer reached the node. A pointer at an odd address, and a tag of 9, were both refused. Language runtimes and kernels use this (small-integer tagging in dynamic language interpreters, the low bits of a `next` pointer in lock-free lists, from memory). The other form of tagging uses the **high** bits, which are unused in current x86-64 user addresses (48 bits are canonical) and are defined for tagging on some ARM64 configurations (top-byte ignore, pointer authentication codes and memory tagging, from memory). That is hardware- and kernel-specific; code that does it must mask before every dereference on the targets that do not ignore the bits, and the standard calls the integer-to-pointer conversion implementation-defined.

## 7. Function pointers in depth

A function pointer holds the address of code. You have met the syntax in the drills (4, 5, 8, 15 to 19). Four patterns carry nearly all real use.

### 7.1 Typedef it, store it, return it

```c
#include <stdio.h>
#include <stdlib.h>
#include "../code/ptr.h"

typedef int (*BinOp)(int, int);

static int add(int a, int b) { return a + b; }
static int sub(int a, int b) { return a - b; }
static int mul(int a, int b) { return a * b; }

/* A function that returns a function pointer. */
static BinOp lookup(char c)
{
    switch (c) {
    case '+': return add;
    case '-': return sub;
    case '*': return mul;
    default: return NULL;
    }
}

/* Callback with a context pointer: the library cannot know the caller's state. */
static void for_each(const int *a, size_t n, void (*cb)(int, void *), void *ctx)
{
    for (size_t i = 0; i < n; ++i)
        cb(a[i], ctx);
}

typedef struct { long sum; int count; int max; } Stats;

static void collect(int x, void *ctx)
{
    Stats *s = ctx;
    s->sum += x;
    if (s->count++ == 0 || x > s->max)
        s->max = x;
}

static int by_dir(const void *a, const void *b, void *ctx)
{
    int x = *(const int *)a, y = *(const int *)b, dir = *(int *)ctx;
    return dir * ((x > y) - (x < y));
}

int main(void)
{
    static const BinOp table[] = {add, sub, mul};
    static const char *names[] = {"add", "sub", "mul"};
    for (int i = 0; i < 3; ++i)
        printf("table[%d] %s(7, 3) = %d\n", i, names[i], table[i](7, 3));
    BinOp f = lookup('*');
    printf("lookup('*') is mul: %s;  lookup('/') is %s\n", f == mul ? "yes" : "no", lookup('/') ? "non-NULL" : "NULL");

    int data[] = {5, -2, 9, 4};
    Stats st = {0, 0, 0};
    for_each(data, 4, collect, &st);
    printf("for_each with a context: sum=%ld count=%d max=%d\n", st.sum, st.count, st.max);

    int up = 1, down = -1;
    sort_ctx(data, 4, sizeof data[0], by_dir, &up);
    printf("sort_ctx ascending: %d %d %d %d\n", data[0], data[1], data[2], data[3]);
    sort_ctx(data, 4, sizeof data[0], by_dir, &down);
    printf("sort_ctx descending (same function, different context): %d %d %d %d\n", data[0], data[1], data[2], data[3]);
    return 0;
}
```

```text
table[0] add(7, 3) = 10
table[1] sub(7, 3) = 4
table[2] mul(7, 3) = 21
lookup('*') is mul: yes;  lookup('/') is NULL
for_each with a context: sum=16 count=4 max=9
sort_ctx ascending: -2 4 5 9
sort_ctx descending (same function, different context): 9 5 4 -2
```

- `typedef int (*BinOp)(int, int);` turns a drill-level declaration into a readable name.
- **A table of pointers** (`table[]`) is a jump table: choose the behaviour by index. The command dispatchers of C 06 (X-macros) and device drivers' operation tables use this.
- **A function that returns a function pointer** (`lookup`) is readable with the typedef: `BinOp lookup(char c)`. Return NULL for "not found" and check it before the call.
- **Calling through NULL or through a pointer of the wrong type** is undefined. Casting a function pointer to a different function type and calling it is undefined. POSIX requires a `void *` returned by `dlsym` to convert to a function pointer (C 06), but plain ISO C does not guarantee that a `void *` can hold one.

### 7.2 Callbacks with a context pointer

C has no closures. A callback that needs to know something (a running total, a file, a direction) gets it through an extra `void *ctx` argument that the caller supplies and the callee passes back unchanged. `for_each(a, n, collect, &st)` above accumulated a sum, a count and a maximum into the caller's `Stats` struct. `qsort` has no context argument, which is why glibc added `qsort_r` and the POSIX standard later adopted a form of it (the argument order differs between glibc and the BSDs, from memory: check the manual of the system you target). The version with the context is the one worth writing:

```c
typedef int (*CmpCtx)(const void *a, const void *b, void *ctx);

static void swap_bytes(void *a, void *b, size_t n)
{
    unsigned char *x = a, *y = b;
    while (n--) {
        unsigned char t = *x;
        *x++ = *y;
        *y++ = t;
    }
}

static void sift_down(unsigned char *base, size_t n, size_t size, size_t i, CmpCtx cmp, void *ctx)
{
    for (;;) {
        size_t l = 2 * i + 1, r = l + 1, m = i;
        if (l < n && cmp(base + l * size, base + m * size, ctx) > 0)
            m = l;
        if (r < n && cmp(base + r * size, base + m * size, ctx) > 0)
            m = r;
        if (m == i)
            return;
        swap_bytes(base + i * size, base + m * size, size);
        i = m;
    }
}

/* Generic heapsort: any element size, comparator gets a context pointer (qsort_r style). */
static void sort_ctx(void *base, size_t n, size_t size, CmpCtx cmp, void *ctx)
{
    unsigned char *b = base;
    if (n < 2)
        return;
    for (size_t i = n / 2; i-- > 0;)
        sift_down(b, n, size, i, cmp, ctx);
    for (size_t end = n - 1; end > 0; --end) {
        swap_bytes(b, b + end * size, size);
        sift_down(b, end, size, 0, cmp, ctx);
    }
}

/* The same algorithm written once per element type: no function pointer, no byte swaps. */
#define DEFINE_HEAPSORT(NAME, T, GREATER)                                        \
    static void NAME##_sift(T *a, size_t n, size_t i)                            \
    {                                                                            \
        for (;;) {                                                               \
            size_t l = 2 * i + 1, r = l + 1, m = i;                              \
            if (l < n && GREATER(a[l], a[m]))                                    \
                m = l;                                                           \
            if (r < n && GREATER(a[r], a[m]))                                    \
                m = r;                                                           \
            if (m == i)                                                          \
                return;                                                          \
            T t = a[i];                                                          \
            a[i] = a[m];                                                         \
            a[m] = t;                                                            \
            i = m;                                                               \
        }                                                                        \
    }                                                                            \
    static void NAME(T *a, size_t n)                                             \
    {                                                                            \
        if (n < 2)                                                               \
            return;                                                              \
        for (size_t i = n / 2; i-- > 0;)                                         \
            NAME##_sift(a, n, i);                                                \
        for (size_t e = n - 1; e > 0; --e) {                                     \
            T t = a[0];                                                          \
            a[0] = a[e];                                                         \
            a[e] = t;                                                            \
            NAME##_sift(a, e, 0);                                                \
        }                                                                        \
    }
#define INT_GT(x, y) ((x) > (y))
DEFINE_HEAPSORT(heapsort_int, int, INT_GT)
```

`sort_ctx` is a heapsort over any element size. The comparator gets two pointers to elements and the context, so one comparator serves ascending and descending order: the last two lines of the output sorted the same array both ways with the **same function** and a different `dir` in the context. The test compares it with `qsort` on 2,000 random arrays and on 24 byte records, and checks every sorted record kept its payload (a deliberate bug that skipped one byte of each swap was caught, as was a heapify that started at the wrong level).

**What the generality costs.** The same algorithm, written once per type with a macro (`DEFINE_HEAPSORT`, the second half of the listing), against the generic version, against the library `qsort`, on 2,000,000 random `int`s:

```text
T1 sort 2000000 ints: checksums generic heapsort 7374319382044534196, specialised heapsort 7374319382044534196, library qsort 7374319382044534196
```

| Sorting 2,000,000 ints | Time ms (run 1 / run 2) |
|---|---|
| `sort_ctx` (generic: indirect comparator call, byte-wise swaps) | 473.00 / 473.00 |
| `heapsort_int` (same algorithm, compiled for `int`) | 235.00 / 233.00 |
| library `qsort` | 344.00 / 350.00 |

All three produced the same checksum. The type-specialised version was about twice as fast as the generic one, because the comparison is inlined and the swap is one register move. The library `qsort` is faster than my generic heapsort (I do not know which algorithm this glibc uses and do not claim one; it is a different algorithm from mine, so the comparison with `sort_ctx` is not like for like) and slower than the specialised version. This is the argument for C++ templates and for macro-generated code in performance-critical C, and the reason kernels write type-specific `list_sort` helpers (from memory).

### 7.3 A table-driven state machine

A state machine is an enum of states, an enum of events and a table that says, for each pair, the next state and the action to run. Adding behaviour means editing the table, not the code. This is a simplified connection state machine in the style of TCP:

```c
typedef enum { S_CLOSED, S_LISTEN, S_SYN_RCVD, S_ESTABLISHED, S_CLOSE_WAIT, NSTATES } State;
typedef enum { E_LISTEN, E_SYN, E_ACK, E_FIN, E_CLOSE, E_RST, NEVENTS } Event;

typedef struct Conn Conn;
typedef void (*Action)(Conn *);
struct Conn {
    State s;
    unsigned accepted, resets, closed;
};

static void act_none(Conn *c) { (void)c; }
static void act_accept(Conn *c) { c->accepted++; }
static void act_reset(Conn *c) { c->resets++; }
static void act_close(Conn *c) { c->closed++; }

typedef struct {
    State next;
    Action act;
} Trans;

#define STAY(s) { s, act_none }

/* Every entry is written out: a zero entry would mean "go to CLOSED and call NULL". */
static const Trans TABLE[NSTATES][NEVENTS] = {
    /*               LISTEN                  SYN                          ACK                             FIN                        CLOSE                      RST */
    [S_CLOSED]      = { {S_LISTEN, act_none}, STAY(S_CLOSED),             STAY(S_CLOSED),                 STAY(S_CLOSED),            STAY(S_CLOSED),            STAY(S_CLOSED) },
    [S_LISTEN]      = { STAY(S_LISTEN),       {S_SYN_RCVD, act_none},     STAY(S_LISTEN),                 STAY(S_LISTEN),            {S_CLOSED, act_close},     STAY(S_LISTEN) },
    [S_SYN_RCVD]    = { STAY(S_SYN_RCVD),     STAY(S_SYN_RCVD),           {S_ESTABLISHED, act_accept},    STAY(S_SYN_RCVD),          {S_CLOSED, act_close},     {S_LISTEN, act_reset} },
    [S_ESTABLISHED] = { STAY(S_ESTABLISHED),  STAY(S_ESTABLISHED),        STAY(S_ESTABLISHED),            {S_CLOSE_WAIT, act_none},  {S_CLOSED, act_close},     {S_CLOSED, act_reset} },
    [S_CLOSE_WAIT]  = { STAY(S_CLOSE_WAIT),   STAY(S_CLOSE_WAIT),         STAY(S_CLOSE_WAIT),             STAY(S_CLOSE_WAIT),        {S_CLOSED, act_close},     {S_CLOSED, act_reset} },
};

static void fsm_step(Conn *c, Event e)
{
    const Trans *t = &TABLE[c->s][e];
    t->act(c);
    c->s = t->next;
}
```

Two design points. **Every entry is written out.** In a table with designated initialisers, a missing entry is zero: "go to `S_CLOSED`, call a NULL function", and the program crashes the first time that pair happens. `STAY(s)` makes the default explicit. And the table is `const`, so it sits in `.rodata` (C 06) and cannot be corrupted at run time. The test compares **every one of the 30 entries** against an independent `switch` implementation and then runs 5,000 random 40-event sequences on both, comparing the state and the three counters after each event; three deliberate bugs in the table (a wrong destination, a wrong action, a dropped transition) were all caught.

```text
CLOSED       + LISTEN -> LISTEN       (accepted 0, resets 0, closed 0)
LISTEN       + SYN    -> SYN_RCVD     (accepted 0, resets 0, closed 0)
SYN_RCVD     + RST    -> LISTEN       (accepted 0, resets 1, closed 0)
LISTEN       + SYN    -> SYN_RCVD     (accepted 0, resets 1, closed 0)
SYN_RCVD     + ACK    -> ESTABLISHED  (accepted 1, resets 1, closed 0)
ESTABLISHED  + FIN    -> CLOSE_WAIT   (accepted 1, resets 1, closed 0)
CLOSE_WAIT   + ACK    -> CLOSE_WAIT   (accepted 1, resets 1, closed 0)
CLOSE_WAIT   + CLOSE  -> CLOSED       (accepted 1, resets 1, closed 1)
CLOSED       + FIN    -> CLOSED       (accepted 1, resets 1, closed 1)
```

The script is a realistic handshake: listen, receive a SYN, get reset back to listening, a second SYN, the ACK that establishes the connection (the `accepted` counter goes to 1), a FIN from the peer, an ACK that is ignored in `CLOSE_WAIT`, the local close, and a FIN to a closed connection, which is ignored. Which is faster, the table or a `switch`?

```text
T2 209715200 events through the state machine: table state 2 accepted 3654399 resets 5477199 closed 16523800; switch state 2 accepted 3654399 resets 5477199 closed 16523800
```

| 209,715,200 events | Time ms (run 1 / run 2) |
|---|---|
| table plus function-pointer actions | 1102.00 / 1088.00 |
| `switch` with the actions written inline | 648.00 / 655.00 |

The table version was about 1.7 times slower. This is not "tables are slow": the difference is the **indirect call** that each step makes through `t->act`, with a target that depends on random data, against a `switch` whose actions the compiler inlines. Where the actions are simple and the machine is on a hot path, the compiler can do better with code than with data. Where the machine has dozens of states and changes often, the table is the maintainable choice, and 5 ns per event is rarely the bottleneck.

### 7.4 What an indirect call costs

A call through a pointer is an indirect jump. The CPU predicts its target; when the prediction is right it costs a little more than a direct call, and when it is wrong the pipeline restarts. 200,000,000 dependent calls, with the same arithmetic in every row:

```text
T3 200000000 dependent calls: inline 1815327454174810215, direct 1815327454174810215, indirect same target 1815327454174810215, indirect pattern of 4 targets 8042174424554935486, indirect random of 4 targets 1170045504649946115
```

| 200,000,000 calls | Time ms (run 1 / run 2) |
|---|---|
| inlined function | 302.00 / 306.00 |
| direct call, `noinline` | 303.00 / 308.00 |
| indirect call, always the same target | 352.00 / 360.00 |
| indirect, 4 targets in a fixed rotation | 423.00 / 429.00 |
| indirect, 4 targets chosen at random | 1845.00 / 1826.00 |

(The checksums differ between the last two rows because they call different functions; the first three rows compute the same function and agree.) The direct `noinline` call cost nothing over inlining here, because each iteration depends on the previous one and the call overhead hides inside that chain. An indirect call to a constant target cost about 16 to 18 percent more, a rotating pattern of four targets about 40 percent more (the predictor learns the pattern, mostly), and **random targets cost about six times as much**: roughly 7.6 ns, or about 16 cycles, per call. That is a branch misprediction. It is why virtual-call-heavy code and table-driven interpreters slow down on unpredictable data, why sorting before dispatching (grouping by type) is a known optimisation, and why "polymorphism is free" is true only for predictable targets.

## 8. What it costs

| Question | Answer (this machine, GCC 13.3) |
|---|---|
| ASan | caught the bugs of sections 3.1 and 3.4 to 3.9 here; programs run about 2 times slower (from memory) |
| A generic comparator-callback sort against the same algorithm specialised for `int` | about 2 times slower |
| A table-driven state machine with function-pointer actions against an inlined `switch` | about 1.7 times slower for 200 million random events |
| Direct, non-inlined call in a dependent chain | no measurable cost over inlining |
| Indirect call, predictable target | 16 to 40 percent more than direct in a call-bound loop |
| Indirect call, unpredictable target | about 6 times a direct call: about 16 cycles per misprediction |
| `realloc` growth by doubling | O(1) amortised; each element copied about twice |
| `FREE(p)` (free and clear) | one store; the price of the safety is nothing |
| Tagging a pointer | one OR to tag, one AND to strip |

## 9. Common mistakes

1. **Reading `int *a[5]` as a pointer to an array.** Postfix binds first: it is an array of pointers. Use a typedef.
2. **`*p++` where `(*p)++` was meant.** The first moves the pointer; the second increments the value.
3. **Dereferencing a pointer before checking it,** or checking it after: the optimiser may delete the late check.
4. **Returning the address of a local** (or storing it somewhere that outlives the call).
5. **Using a pointer after `free`,** or after `realloc` moved the block, or after a vector grew.
6. **`p = realloc(p, n)`.** On failure you lose the old block. Use a temporary.
7. **Freeing a moved pointer, a stack address, or a pointer to the middle of a block.**
8. **Two owners for one allocation,** so it is freed twice. Decide who owns each block.
9. **`malloc(strlen(s))`.** The terminator needs a byte: `+ 1` or `strdup`.
10. **A function that frees its argument and then "clears" it.** It clears a copy. Take a `T **`, or use a macro.
11. **Casting pointers to different types and reading through them.** Strict aliasing makes the result depend on the optimiser. Use `memcpy`.
12. **Large VLAs and unbounded recursion.** A stack overflow that input can trigger.
13. **A function pointer table with a zero entry,** called without a check.
14. **Keeping an element pointer into a growable container across an insertion.**

## 10. Questions out loud

1. **How do you read a complicated declaration?** Start at the identifier, go right (arrays and function parameters), go left (`*`, qualifiers), step out of parentheses and continue; or build it from typedefs.
2. **What is the difference between `const int *p`, `int *const p` and `const int *const p`?** Pointer to constant int; constant pointer to int; constant pointer to constant int.
3. **What does `*p++` do?** It reads `*p`, then advances `p`; postfix `++` binds tighter than `*`.
4. **What is a dangling pointer, and what are the three ways to get one?** A pointer to storage that no longer exists: a returned local, a freed block, a block moved by `realloc` (or a container that grew).
5. **How do you find a use-after-free?** AddressSanitizer reports the access, the free and the allocation; valgrind does the same more slowly.
6. **What is the safe way to use `realloc`?** Assign the result to a temporary, check it, then replace the original; on failure the original is still valid.
7. **Why can't a function free a pointer and set the caller's copy to NULL?** Arguments are passed by value; it needs the address of the pointer (`T **`).
8. **What does `free` accept?** A pointer returned by `malloc`, `calloc` or `realloc`, or NULL; nothing else, and not twice.
9. **What does `malloc(0)` return?** Implementation-defined: NULL or a unique pointer; glibc returns a unique pointer.
10. **What is strict aliasing?** The compiler assumes pointers of unrelated types do not point at the same object, so it may reorder or drop accesses; punning through a cast is undefined. `memcpy` is the legal way.
11. **`SIGSEGV` against `SIGBUS`?** Segmentation fault: no valid mapping or wrong permission. Bus error: the address is mapped but cannot be backed (a truncated mapped file; a misaligned access on some architectures).
12. **How can you store a tag in a pointer?** Use the low bits that alignment guarantees are zero, or high bits where the hardware ignores them; strip the tag before use.
13. **How do you pass state to a callback in C?** An extra `void *` context argument, as in `qsort_r`.
14. **What is a table-driven state machine and why write every entry?** A 2-D table of next state and action; a missing entry defaults to zero, which means a wrong state and a NULL call.
15. **What does an indirect call cost?** Little when the target is predictable, a pipeline flush (about 16 cycles here) when it is not.
16. **What are the limits on pointer comparison and subtraction?** Defined only within one array (or one past its end).

---

## Problems

Difficulty is marked E (entry level), M (medium) or H (hard). Do A and B on paper first.

**A (E, on paper).** Read each declaration aloud and write the English description; then write the declaration of `handlers`, an array of 8 pointers to functions taking an `int` and a pointer to `void` and returning `int`.

```text
1.  char *(*(*x1)[3])(void);
2.  void (*(*x2)(int))(void);
3.  int (*const (*x3)[2])(void);
4.  const int *const *const x4;
5.  int (*(*(*x5)(int))[4])(char);
6.  int (*x6[8])(int, void *);        // x6 is the declaration you are asked to write
```

**B (E, on paper).** Starting from `int a[5] = {1, 2, 3, 4, 5}; int *p = a, *q = a + 4; int v;`, run these statements in order and write the array and the positions of `p` and `q` after each, then `v`, then `p - q` and `p > q` after the final adjustment:

```text
*p++ = *q--;
*++p = *q;
++*p;
v = (*q)++;
p += 2;  q -= 1;
```

**C (M, with a sanitizer).** The program below (`ansC.c`) has five separate bugs, selected by an argument 1 to 5. For each, run it under `-fsanitize=address`, name the bug from the report, and write the one-line fix:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void consume(char *p) { printf("consumed %c\n", p[0]); free(p); }

static int early_return(int fail)
{
    char *buf = malloc(64);
    if (fail)
        return -1;
    free(buf);
    return 0;
}

int main(int argc, char **argv)
{
    int bug = argc > 1 ? atoi(argv[1]) : 0;
    const char *name = "abcdefgh";
    char *p = NULL, *q = NULL;
    switch (bug) {
    case 1:
        p = malloc(strlen(name));
        strcpy(p, name);
        free(p);
        break;
    case 2:
        p = strdup(name);
        consume(p);
        printf("after consume: %c\n", p[0]);
        break;
    case 3:
        p = malloc(8);
        q = p;
        strcpy(p, "abc");
        p = realloc(p, 4096);
        printf("old pointer still reads: %c\n", q[0]);
        free(p);
        break;
    case 4:
        p = malloc(8);
        q = p;
        free(p);
        free(q);
        break;
    case 5:
        early_return(1);
        break;
    }
    return 0;
}
```

**D (M, in your editor).** Extend the `Vec` of section 4 with `vec_pop`, `vec_insert(v, index, x)` and `vec_remove(v, index)` (using `memmove`), and `vec_shrink` (return unused capacity with `realloc`, keeping the vector valid if `realloc` fails). Write the tests first: compare with a plain array reference on random operation sequences, include index 0, index `len`, and out-of-range indices (return -1, vector unchanged), and inject allocation failures at every call. Run under ASan and LeakSanitizer.

**E (H, in your editor).** A line-oriented protocol sends commands like `PUT key value`, `GET key` and `QUIT`. Write the parser as a **table-driven state machine with function-pointer states**: each state is a function `State (*)(Parser *, int ch)` that consumes one character and returns the next state, and the table of actions is data. Handle a line split across any number of reads (feed the parser one byte at a time). Test it by feeding each of 200 random valid and invalid command lines one byte at a time and in random chunk sizes, and comparing against a reference that uses `fgets` and `strtok_r`.

**Where to solve them.** A and B on paper, then check against the answers. C, D, E in your editor with `gcc -std=gnu11 -Wall -Wextra -g -fsanitize=address,undefined`. The listings in sections 4, 6 and 7 are the style to follow.

**How to approach them.**

- **A:** apply the five steps of section 1. Write the typedef chain, one layer per line, and ask the compiler to check it with `_Static_assert`, as the drills do. For 4, read the `const`s from the identifier outwards. For 6, the identifier is followed by `[8]` first.
- **B:** write the array as five boxes and draw `p` and `q` as arrows. `*p++ = *q--` is `*(p++) = *(q--)`: read `*q`, store it at `*p`, then move both.
- **C:** run each bug separately. For each, find the access in the report, then find the allocation or the free. Decide whether the fix is a size, an ownership change, an index instead of a pointer, or a cleanup path.
- **D:** `vec_insert` needs room first (grow, then `memmove`), and `vec_remove` moves the tail down by one element. For `vec_shrink` use a temporary and keep the old block if `realloc` returns NULL.
- **E:** write the reference first, then the states. Think about what "one byte at a time" does to a state that is in the middle of a word.

## Answers

**A.** The compiler checked these readings (the answers are written as typedef chains and compared with the declarations):

```text
all 6 answers compile: each declaration has the type of its English description
```

1. `x1` is a pointer to an array of 3 pointers to function (void) returning pointer to `char`. 2. `x2` is a pointer to a function taking `int` and returning a pointer to a function taking no arguments and returning `void`. 3. `x3` is a pointer to an array of 2 constant pointers to function (void) returning `int`. 4. `x4` is a constant pointer to a constant pointer to a constant `int`: nothing in the chain can be reassigned or written. 5. `x5` is a pointer to a function taking `int` and returning a pointer to an array of 4 pointers to functions that take a `char` and return `int`. 6. `int (*handlers[8])(int, void *);`, or with typedefs: `typedef int Handler(int, void *); Handler *handlers[8];`.

**B.** The real output:

```text
*p++ = *q--;   a = {5, 2, 3, 4, 5}   p = a+1   q = a+3
*++p = *q;     a = {5, 2, 4, 4, 5}   p = a+2   q = a+3
++*p;          a = {5, 2, 5, 4, 5}   p = a+2   q = a+3
v = (*q)++;    a = {5, 2, 5, 5, 5}   p = a+2   q = a+3
v = 4
p += 2; q -= 1;  p - q = 2   (p > q) = 1
```

`*p++ = *q--` stored 5 over `a[0]` and moved `p` to `a[1]` and `q` to `a[3]`. `*++p = *q` first moved `p` to `a[2]`, then stored `a[3]` (4) there. `++*p` incremented `a[2]` to 5. `(*q)++` returned the old `a[3]`, 4, and made it 5. After `p += 2` (to `a[4]`) and `q -= 1` (to `a[2]`), `p - q` is 2 elements and `p > q` is true. Pointer subtraction is in elements, not bytes, and both pointers are in the same array.

**C.** The five reports:

```text
bug 1: SUMMARY: AddressSanitizer: heap-buffer-overflow asan_interceptors.cpp:563 in strcpy
bug 2: SUMMARY: AddressSanitizer: heap-use-after-free ansC.c:30 in main
bug 3: SUMMARY: AddressSanitizer: heap-use-after-free ansC.c:37 in main
bug 4: SUMMARY: AddressSanitizer: double-free asan_malloc_linux.cpp:52 in free
bug 5: SUMMARY: AddressSanitizer: 64 byte(s) leaked in 1 allocation(s).
```

Bug 1: `malloc(strlen(name))` is one byte short for `strcpy`; allocate `strlen(name) + 1`. Bug 2: `consume` freed the block and `main` read it after; the caller must not use a pointer it handed to a function that takes ownership (set it to `NULL` after the call, or make `consume` take `char **`). Bug 3: `q` is a second pointer to a block that `realloc` moved; use only the pointer `realloc` returned, or hold an index. Bug 4: `p` and `q` are two owners of one block, freed twice; free once, through the owner, and clear the pointers. Bug 5: `early_return(1)` returns before the `free`; free on every path (a single exit with a `goto cleanup`, C 10). Notice which reports name the line and which name a library frame: for bugs 1 and 4 the summary line points into the sanitizer's copy of `strcpy` and `free`, and the frame above it in the full report is the one that is in your code.

**D.** Outline. `vec_insert`: if `index > len` return -1; ensure capacity for one more (same temporary-pointer pattern as `vec_push`, return -1 on failure with the vector unchanged); `memmove(&d[index + 1], &d[index], (len - index) * sizeof *d)`; store; increment `len`. `vec_remove`: if `index >= len` return -1; `memmove(&d[index], &d[index + 1], (len - index - 1) * sizeof *d)`; decrement `len`. `vec_pop` is `vec_remove` at `len - 1` (and it returns the value first). `vec_shrink`: `p = realloc(data, len * sizeof *p)`; if `p` is NULL keep the old block; special-case `len == 0`, because `realloc(p, 0)` has differing meanings (section 3.11): free the block and set `data` to NULL, `cap` to 0 yourself. The reference is a plain array with the same operations by loops. The failure test sets the allocator to fail at call k for k from 0 to 8 and checks that each vector is identical before and after a failed call.

**E.** Outline. The parser struct holds the current state function, a small buffer for the current word, a count of words, and the line so far. Each state function looks at one character: `st_start` skips spaces and on a letter begins the command word; `st_cmd` accumulates letters until a space or newline, then decides `PUT`, `GET` or `QUIT` and returns `st_key` or `st_done`; `st_key` and `st_value` similarly. A newline in the wrong state returns `st_error`, which discards characters to the end of the line and then returns `st_start`. Because the state is stored in the parser between calls, feeding one byte at a time works by construction: that is the property to test. The reference uses `fgets` and `strtok_r` on the same line and the test compares the accepted commands and the error lines. Mutation test it as the table of section 7.3 was: change one transition and see that the random test notices.

## Cheat sheet

- **Declarations:** identifier, right (arrays, parameters), left (`*`, qualifiers), step out of parentheses; postfix binds tighter than `*`; typedef each layer; `int *a[5]` array of pointers, `int (*a)[5]` pointer to array; `const` applies to its left.
- **Increment forms:** `*p++` read then advance; `(*p)++` read then increment value; `*++p` advance then read; `++*p` increment value then read.
- **The bug list:** null; uninitialised; dangling local; use after free; double free; stale after `realloc`; leak; bad free (stack, middle of a block); off-by-one on `malloc(strlen)`; callee frees a copy; `malloc(0)`; aliasing; misaligned; `SIGBUS`; stack overflow.
- **Tools:** warnings first; ASan for memory errors and leaks; UBSan for undefined behaviour; valgrind for uninitialised values; glibc aborts on some double frees.
- **Rules:** `p = realloc(p, n)` leaks on failure; clear pointers after free (`FREE`); pass `T **` to clear the caller's pointer; keep an index into a growable container, not a pointer; `+ 1` for the terminator; free exactly what `malloc` returned, once.
- **Integers:** `uintptr_t` for address arithmetic; `align_up` is `(x + a - 1) & ~(a - 1)`; low bits of an aligned pointer can hold a tag; compare and subtract pointers only within one array.
- **Aliasing:** unrelated pointer types do not alias; use `memcpy`, a union or `char *`; `-fno-strict-aliasing` hides it, it does not fix it.
- **Function pointers:** typedef them; table for dispatch; check for NULL; callbacks take a `void *` context; a state machine is a table of (next state, action), every entry written out.
- **Costs:** a type-specialised sort about 2 times a generic one; indirect calls cost about 15 to 40 percent when predictable and about 6 times when random.

## Sources

- [Sanitizers: AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer in the GCC manual](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html): the flags used for every bug demo.
- [AddressSanitizer algorithm and report format (Google sanitizers wiki)](https://github.com/google/sanitizers/wiki/AddressSanitizerAlgorithm): how the shadow memory finds the errors.
- [realloc(3), Linux man pages](https://man7.org/linux/man-pages/man3/realloc.3.html): the failure behaviour and the `size == 0` note.
- [malloc(3), Linux man pages](https://man7.org/linux/man-pages/man3/malloc.3.html): `malloc(0)` and the diagnostics glibc prints.
- [sigaction(2), Linux man pages](https://man7.org/linux/man-pages/man2/sigaction.2.html): `SIGSEGV` and `SIGBUS`, and the alternate signal stack used in section 3.15.
- [GCC manual: Optimize Options, `-fstrict-aliasing`](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html): the aliasing assumption demonstrated in section 3.12.
- [qsort(3), Linux man pages](https://man7.org/linux/man-pages/man3/qsort.3.html): `qsort_r` and the comparator contract.
