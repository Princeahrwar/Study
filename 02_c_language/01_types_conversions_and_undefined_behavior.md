# C 01: Types, Conversions and Undefined Behaviour

*Part of the [C language plan](00_plan.md) · Time: about 3 hours (2 reading, 1 problems) · You need: day-to-day C, and a compiler (`gcc` or `clang`). There is no companion demo file: every example below is a short program that was compiled and run, and its real output is printed under it*

Most C bugs that survive code review are not typos. They are programs that **look** right and **are** right on the machine you tested on, and that break at a different optimisation level, a different compiler, or a different CPU. The cause is nearly always one of two things:

1. the code relied on an **integer conversion** that does not do what the eye expects (`-1 < 1u` is false), or
2. the code triggered **undefined behaviour (UB)**, and the optimiser, which is allowed to assume UB never happens, removed or rewrote part of the program.

This tutorial is the foundation for the rest of the C track. It states exactly what the language promises about integers, walks through the conversion rules step by step, defines the four kinds of "behaviour" the standard distinguishes, catalogues the UB that matters in systems code, and then shows, with compiled output, what an optimiser does with it.

By the end you can:

- say which sizes and ranges C guarantees and which are only what your platform happens to do;
- predict the type and value of any integer expression by applying integer promotion and the usual arithmetic conversions;
- explain why signed overflow is undefined while unsigned overflow wraps, and write overflow checks that are themselves free of UB;
- list the common kinds of UB, say which tool catches which, and explain why a program that "works at `-O0`" proves nothing;
- answer "what does this print?" with "defined, and here is why", "implementation-defined", "unspecified" or "undefined".

**Where the numbers come from.** Every program in this tutorial was compiled with GCC 13.3 (`-std=c11`) on an x86-64 Linux container and run; the outputs shown are what it printed, at the optimisation level named. This machine is **LP64** (`long` and pointers are 64 bits). Things said about ARM64, Windows or 32-bit targets, and about other compilers, are from the standard or from memory and are marked "not measured here". Your Mac (ARM64) will agree with every output below except where a platform difference is called out; run the programs yourself to confirm, and compare `-O0` with `-O2` as the plan advises.

---

## 1. What the standard guarantees about integer types

C defines its integer types by **minimum** ranges, not exact sizes. That is why portable code uses `<stdint.h>` types when the width matters.

| Type | Guaranteed at least | Measured here (x86-64 Linux, LP64) |
|---|---|---|
| `char` | 8 bits (`CHAR_BIT >= 8`); `sizeof(char)` is 1 **by definition** | 1 byte |
| `short` | 16 bits, range at least -32767 to 32767 | 2 bytes |
| `int` | 16 bits, same minimum range | 4 bytes |
| `long` | 32 bits | 8 bytes |
| `long long` | 64 bits | 8 bytes |
| pointers, `size_t` | no promise of size | 8 bytes |

Three promises hold on every platform: the sizes are ordered `char <= short <= int <= long <= long long`; `int` is the "natural" word the machine handles fastest; and `size_t` is an **unsigned** type big enough to index any object.

```c
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
int main(void) {
    printf("char=%zu short=%zu int=%zu long=%zu long long=%zu pointer=%zu size_t=%zu\n",
           sizeof(char), sizeof(short), sizeof(int), sizeof(long), sizeof(long long), sizeof(void *), sizeof(size_t));
    printf("CHAR_MIN=%d CHAR_MAX=%d INT_MIN=%d INT_MAX=%d\n", CHAR_MIN, CHAR_MAX, INT_MIN, INT_MAX);
    printf("LONG_MAX=%ld UINT_MAX=%u SIZE_MAX=%zu\n", LONG_MAX, UINT_MAX, (size_t)SIZE_MAX);
    char c = (char)0xFF;
    printf("char c = 0xFF: c == 0xFF is %d, c < 0 is %d\n", c == 0xFF, c < 0);
    return 0;
}
```

```
char=1 short=2 int=4 long=8 long long=8 pointer=8 size_t=8
CHAR_MIN=-128 CHAR_MAX=127 INT_MIN=-2147483648 INT_MAX=2147483647
LONG_MAX=9223372036854775807 UINT_MAX=4294967295 SIZE_MAX=18446744073709551615
char c = 0xFF: c == 0xFF is 0, c < 0 is 1
exit=0
```

Reading the output:

- `long` is 8 bytes here because Linux and macOS on 64-bit CPUs use **LP64**. 64-bit Windows uses **LLP64** (`long` stays 32 bits, `long long` and pointers are 64) and 32-bit systems use **ILP32** (`int`, `long` and pointers are 32). Not measured here; this is why the standard's minimums matter. Never assume `sizeof(long) == sizeof(void *)`; use `intptr_t` or `uintptr_t` for that, and `size_t` for sizes.
- **Plain `char` has no guaranteed signedness.** Here it is signed (`CHAR_MIN` is -128), so `char c = (char)0xFF` holds -1, `c == 0xFF` is false (the -1 is promoted to int and compared with 255), and `c < 0` is true. Compiling the same file with `-funsigned-char` gives:

```
char=1 short=2 int=4 long=8 long long=8 pointer=8 size_t=8
CHAR_MIN=0 CHAR_MAX=255 INT_MIN=-2147483648 INT_MAX=2147483647
LONG_MAX=9223372036854775807 UINT_MAX=4294967295 SIZE_MAX=18446744073709551615
char c = 0xFF: c == 0xFF is 1, c < 0 is 0
```

  Now `CHAR_MIN` is 0 and the comparison flips. That is the behaviour of many ARM Linux targets (not measured here; Apple's ARM64 ABI, from memory, keeps `char` signed). A test that passes on x86 can fail on ARM for this reason alone. Rule: use `unsigned char` or `uint8_t` for bytes, and plain `char` only for text.
- **The fixed-width types** `int8_t` to `int64_t` and `uint8_t` to `uint64_t` (`<stdint.h>`) have exactly that width and, for signed ones, a two's-complement representation. Use them for registers, file formats and packets. `int_fast16_t` and `int_least16_t` exist but are rarely what you want.

**Representation.** C11 allows three signed representations (two's complement, ones' complement, sign-magnitude). C23 requires two's complement, and every machine you will meet already uses it. In an `N`-bit two's complement type the range is `-2^(N-1)` to `2^(N-1) - 1`: there is one more negative value than positive. That asymmetry is the source of `-INT_MIN` and `abs(INT_MIN)` bugs, shown below. An `N`-bit unsigned type holds `0` to `2^N - 1` and does arithmetic **modulo `2^N`**.

## 2. Integer promotion and the usual arithmetic conversions

Arithmetic in C never happens on types smaller than `int`. Two steps, in order, decide the type of `a + b`:

**Step 1: integer promotion.** Any operand whose type has a lower rank than `int` (`char`, `signed char`, `unsigned char`, `short`, `unsigned short`, `_Bool`, bit-fields) is converted to `int` if `int` can represent every value of its type; otherwise to `unsigned int`. On any normal machine that means **all the small types become `int`**, even `unsigned char` and `unsigned short`.

**Step 2: the usual arithmetic conversions**, applied to the two (now promoted) operands. If either is floating point, the other becomes the wider floating type. For integers:

1. same type: done;
2. both signed, or both unsigned: the lower-rank one is converted to the higher-rank type;
3. the unsigned operand has rank **greater than or equal** to the signed one: the signed operand is converted to the **unsigned** type;
4. otherwise, if the signed type can represent every value of the unsigned type: the unsigned one is converted to the signed type;
5. otherwise both are converted to the unsigned version of the signed type.

Rule 3 is the trap: **mixing signed and unsigned of the same rank turns the signed value unsigned**, and a negative number becomes a very large one. Rule 4 explains why the result depends on the platform: `-1L < 1u` converts `1u` to `long` (a 64-bit `long` can hold every `unsigned int`), so it is true on LP64 (the last line of the output below shows 1); where `long` and `int` are both 32 bits it falls to rule 3 and is false (not measured here).

Comparison, addition, subtraction, multiplication, division and the bitwise operators all use these rules. Shifts are different: each operand is promoted separately and the result has the type of the promoted **left** operand.

```c
#include <stdio.h>
int main(void) {
    unsigned char a = 200, b = 100;
    printf("a + b = %d\n", a + b);               /* both promoted to int: 300 */
    unsigned char c = a + b;                     /* converted back: 300 mod 256 */
    printf("stored in an unsigned char: %d\n", c);
    printf("-1 < 1u is %d\n", -1 < 1u);          /* -1 becomes a huge unsigned number */
    int i = -1; unsigned u = 1;
    printf("i < u is %d\n", i < u);
    printf("sizeof(int) > -1 is %d\n", sizeof(int) > -1);   /* sizeof is size_t, unsigned */
    printf("-1L < 1u is %d\n", -1L < 1u);     /* long can hold every unsigned int here, so both become long */
    unsigned char m = 0;
    printf("~m = %d\n", ~m);                     /* m promoted to int first: ~0 = -1 */
    printf("(unsigned char)~m = %d\n", (unsigned char)~m);
    signed char s = -1; unsigned int t = s;
    printf("signed char -1 to unsigned int: %u\n", t);
    size_t n = 0;
    printf("n - 1 for an unsigned n of 0: %zu\n", n - 1);
    printf("(signed char)200 = %d\n", (signed char)200);  /* implementation-defined, not UB */
    return 0;
}
```

```
a + b = 300
stored in an unsigned char: 44
-1 < 1u is 0
i < u is 0
sizeof(int) > -1 is 0
-1L < 1u is 1
~m = -1
(unsigned char)~m = 255
signed char -1 to unsigned int: 4294967295
n - 1 for an unsigned n of 0: 18446744073709551615
(signed char)200 = -56
exit=0
```

The output is identical at `-O0` and `-O2` because all of it is defined. Line by line:

- `a + b = 300`: both `unsigned char`s were promoted to `int`, added as ints, and 300 is a perfectly good `int`. Nothing wrapped.
- `stored in an unsigned char: 44`: converting 300 to `unsigned char` keeps the value modulo 256 (300 - 256 = 44). Conversion **to an unsigned type is always defined**: reduce modulo `2^N`.
- `-1 < 1u is 0`, `i < u is 0`, `sizeof(int) > -1 is 0`: the `-1` was converted to `unsigned int` (4294967295) or to `size_t`, so the comparison is "huge < small". `-Wsign-compare` (in `-Wextra`) flags the first three:

```
s/promote.c: In function 'main':
s/promote.c:7:34: warning: comparison of integer expressions of different signedness: 'int' and 'unsigned int' [-Wsign-compare]
    7 |     printf("-1 < 1u is %d\n", -1 < 1u);          /* -1 becomes a huge unsigned number */
      |                                  ^
s/promote.c:9:31: warning: comparison of integer expressions of different signedness: 'int' and 'unsigned int' [-Wsign-compare]
    9 |     printf("i < u is %d\n", i < u);
      |                               ^
s/promote.c:10:52: warning: comparison of integer expressions of different signedness: 'long unsigned int' and 'int' [-Wsign-compare]
   10 |     printf("sizeof(int) > -1 is %d\n", sizeof(int) > -1);   /* sizeof is size_t, unsigned */
      |                                                    ^
```

- `~m = -1`: `m` was promoted to `int` **before** the complement, so `~0` is all 32 bits set, which is -1. Casting back to `unsigned char` gives 255.
- `signed char -1 to unsigned int: 4294967295`: the value -1 is converted, not the bit pattern copied: modulo `2^32`.
- `n - 1 for an unsigned n of 0` wraps to `SIZE_MAX`. This is the classic reverse-loop bug: `for (size_t i = n - 1; i >= 0; --i)` never ends because an unsigned `i` is always `>= 0`.
- `(signed char)200 = -56`: converting to a **signed** type a value that does not fit is **implementation-defined** (or raises an implementation-defined signal), not undefined; GCC documents that it wraps modulo 256. Do not rely on it in portable code.

More conversions that appear in interviews:

```c
#include <stdio.h>
int main(void) {
    int i = -5; unsigned u = 3;
    printf("i + u = %u\n", i + u);               /* int converted to unsigned */
    printf("i / 2 = %d, i %% 2 = %d\n", i / 2, i % 2);   /* C99: division truncates toward zero */
    printf("7 / 2 = %d, 7 / 2.0 = %.1f\n", 7 / 2, 7 / 2.0);
    long long big = 1 << 20; long long wide = 1LL << 40;
    printf("1 << 20 = %lld, 1LL << 40 = %lld\n", big, wide);
    int a = 100000, b = 100000;
    long long wrong = a * b;                     /* the multiplication is done in int first */
    printf("100000 * 100000 in int, then widened: %lld\n", wrong);
    return 0;
}
```

```
i + u = 4294967294
i / 2 = -2, i % 2 = -1
7 / 2 = 3, 7 / 2.0 = 3.5
1 << 20 = 1048576, 1LL << 40 = 1099511627776
100000 * 100000 in int, then widened: 1410065408
exit=0
```

- `i + u` with `i = -5` and `u = 3` is computed in unsigned arithmetic: `-5` becomes 4294967291, plus 3, is 4294967294. It prints that, not -2.
- Division **truncates toward zero** (guaranteed since C99), and `%` takes the sign of the dividend: `-5 / 2` is -2 and `-5 % 2` is -1. Before C99 this was implementation-defined.
- `7 / 2` is integer division (3); one floating operand makes the whole division floating (3.5).
- `1 << 20` is an `int` shift, fine; the `long long` needs `1LL << 40`. Writing `1 << 40` is UB (the shift count equals or exceeds the width of the promoted left operand).
- `a * b` with both operands `int` is multiplied **as int**, and 100000 * 100000 = 10^10 does not fit in 32 bits. Widening the result to `long long` afterwards is too late. This particular output (1410065408) is what wraparound looks like on this machine, but the standard calls that overflow undefined; it is shown only because it is what GCC produced here. Write `(long long)a * b`.

## 3. Four kinds of behaviour

The standard separates what a program is guaranteed to do into four classes. Interview answers depend on using the right word.

| Class | Meaning | Example |
|---|---|---|
| **Defined** | The standard says exactly what happens. | Unsigned overflow wraps; `7 / 2` is 3; converting to an unsigned type reduces modulo `2^N`. |
| **Implementation-defined** | Several results allowed; the compiler must pick one and **document** it. | Signedness of plain `char`; right shift of a negative number; converting an out-of-range value to a signed type; `sizeof(int)`. |
| **Unspecified** | Several results allowed; no documentation needed, and it may differ each time. | Order of evaluation of function arguments (`f(g(), h())`); order of evaluation of `a() + b()`. |
| **Undefined (UB)** | **No requirement at all** on the program, including the parts that ran *before* the UB. | Signed overflow; out-of-bounds access; null dereference; a shift by too many bits; modifying an object twice with no sequencing. |

UB is not "the result is some random value". It is a **contract**: the programmer promises it never happens, and in return the compiler may generate code that is only correct if the promise holds. When the promise is broken the program has no meaning, so the compiler's output may crash, may print something plausible, or may delete a check you wrote. Section 5 shows each.

## 4. Signed overflow is undefined, unsigned overflow wraps

```c
#include <limits.h>
#include <stdio.h>
int will_wrap(int x) {
    int y = x + 1;                               /* signed overflow when x is INT_MAX: UB */
    return y < x;                                /* "did it wrap around?" */
}
int main(int argc, char **argv) {
    (void)argv;
    unsigned u = UINT_MAX;
    u = u + 1;                                   /* unsigned arithmetic wraps: defined */
    printf("UINT_MAX + 1 = %u\n", u);
    int big = INT_MAX - 1 + argc;                /* INT_MAX, but not known at compile time */
    printf("will_wrap(INT_MAX) = %d\n", will_wrap(big));
    return 0;
}
```

At `-O0`:

```
UINT_MAX + 1 = 0
will_wrap(INT_MAX) = 1
exit=0
```

At `-O2`:

```
UINT_MAX + 1 = 0
will_wrap(INT_MAX) = 0
exit=0
```

`UINT_MAX + 1` is `0` at both levels: unsigned overflow is defined. But `will_wrap(INT_MAX)` returns 1 at `-O0` (the machine's `add` wrapped, so `y < x`) and **0 at `-O2`**: the optimiser assumed `x + 1` never overflows, so `y` is always greater than `x`, so the function always returns 0. The check was deleted by the optimiser, not by a bug. The assembly (Intel syntax, x86-64) makes it visible; here is the function from the file `opt.c` compiled at both levels:

```
always_greater:
    push    rbp
    mov    rbp, rsp
    mov    DWORD PTR -4[rbp], edi
    mov    eax, 1
    pop    rbp
    ret

will_wrap:
    push    rbp
    mov    rbp, rsp
    mov    DWORD PTR -20[rbp], edi
    mov    eax, DWORD PTR -20[rbp]
    add    eax, 1
    mov    DWORD PTR -4[rbp], eax
    mov    eax, DWORD PTR -4[rbp]
    cmp    eax, DWORD PTR -20[rbp]
    setl    al
    movzx    eax, al
    pop    rbp
    ret
```

```
always_greater:
    mov    eax, 1
    ret

will_wrap:
    xor    eax, eax
    ret
```

Notice two things. At `-O2` the whole `will_wrap` function is `xor eax, eax; ret`: it returns 0 without looking at its argument. And at `-O0` the **first** function, `x + 1 > x`, is already `mov eax, 1`: GCC folds that comparison even with optimisation off, because it is the same assumption. So "I compiled without optimisation, so my overflow check works" is not a safe belief; the version where you store the sum in a variable first happened to survive here, and that is luck, not a guarantee.

Three ways to get the behaviour you actually want:

- **`-fwrapv`** tells GCC and Clang to define signed overflow as wraparound. With it, the same program prints `will_wrap(INT_MAX) = 1` at `-O2`:

```
UINT_MAX + 1 = 0
will_wrap(INT_MAX) = 1
```

  This changes the language the program is written in; Linux kernel code is built with `-fno-strict-overflow` (similar effect, from memory) for exactly this reason. It is a safety net, not a licence to write overflow checks after the fact.
- **`-fsanitize=undefined`** (UBSan) inserts runtime checks and reports the first signed overflow with the file and line:

```
s/overflow.c:4:9: runtime error: signed integer overflow: 2147483647 + 1 cannot be represented in type 'int'
exit=1
```

- **Check before you compute**, which is the portable fix and is shown in section 7.

Other overflow cases, all UB: `-INT_MIN`, `abs(INT_MIN)` (the positive value 2^31 does not exist in an `int`), `INT_MAX + 1`, `a * b` when the product does not fit, and `INT_MIN / -1` (section 5).

## 5. The UB catalogue

Not every UB can be listed, but these are the ones that appear in systems code and in interviews. The right column says how to catch it.

| UB | Example | How to catch it |
|---|---|---|
| Signed integer overflow | `INT_MAX + 1`, `abs(INT_MIN)` | UBSan `-fsanitize=signed-integer-overflow` |
| Shift by a negative count or by `>=` the width; left shift of a negative value; left shift whose result does not fit | `1 << 32`, `1 << 31` (int), `-1 << 1` | UBSan `-fsanitize=shift` |
| Division or remainder by zero; `INT_MIN / -1` | `x / 0` | UBSan; on x86 the CPU also traps (SIGFPE) |
| Out-of-bounds access, or pointer arithmetic beyond one past the end | `a[10]` on `int a[10]` | AddressSanitizer, Valgrind |
| Null pointer dereference | `*p` with `p == NULL` | UBSan `-fsanitize=null`, or the segfault (not reliable) |
| Use after free, double free, returning a pointer to a local | `free(p); *p` | AddressSanitizer |
| Reading an uninitialised automatic variable (whose address is never taken) | `int x; return x;` | `-Wuninitialized`, MemorySanitizer (Clang) |
| Modifying a scalar twice, or modifying and reading it, with no sequencing between | `i = i++ + 1`, `a[i] = i++` | `-Wsequence-point` (compile time) |
| Accessing an object through an lvalue of an incompatible type (strict aliasing) | `*(unsigned *)&f` where `f` is a `float` | `-Wstrict-aliasing` (weak); `-fno-strict-aliasing` to test |
| Misaligned access through a pointer | `*(int *)(buf + 1)` | UBSan `-fsanitize=alignment` |
| Modifying a string literal or a `const` object | `char *p = "abc"; p[0] = 'x';` | segfault (the literal is in read-only memory); `-Wwrite-strings` |
| Using the value of a function that fell off its end without `return` | `int f(void) { }` used as `f()` | `-Wreturn-type` |
| Comparing pointers into different objects with `<`, or subtracting them | `p < q` where they point to different arrays | hard to catch |

The next examples are run, with the two optimisation levels where they matter.

**Shifts.** `1u << 31` is defined. `-1 >> 1` is implementation-defined (arithmetic shift here, giving -1). `1 << 31` on an `int` is UB because 2^31 does not fit:

```c
#include <stdio.h>
int main(int argc, char **argv) {
    (void)argv;
    printf("1u << 31 = %u\n", 1u << 31);         /* fine: unsigned */
    printf("-1 >> 1 = %d\n", -1 >> 1);           /* implementation-defined: arithmetic shift here */
    int k = 30 + argc;                           /* 31 */
    printf("1 << 31 = %d\n", 1 << k);            /* UB: 2^31 does not fit in int */
    return 0;
}
```

```
1u << 31 = 2147483648
-1 >> 1 = -1
1 << 31 = -2147483648
exit=0
```

The optimiser here still produced the "obvious" -2147483648 at both levels, so the code looks fine until UBSan reports it:

```
s/shift.c:7:32: runtime error: left shift of 1 by 31 places cannot be represented in type 'int'
exit=1
```

Notice that this program works and still has UB. That is the dangerous case: it is waiting for a different compiler version, a different optimisation level, or a different CPU. (A shift count of 32 or more behaves differently on x86 and ARM at the machine level because the hardware masks or saturates the count differently; the C standard gives neither a result, so the compiler is free to do either. Not measured here.)

**Division.** On x86 the CPU raises an exception for `INT_MIN / -1`, so the program is killed. The output before the crash is flushed on purpose so you can see it:

```c
#include <limits.h>
#include <stdio.h>
int main(int argc, char **argv) {
    (void)argv;
    int x = INT_MIN + argc - 1;                  /* INT_MIN */
    int y = -argc;                               /* -1 */
    printf("about to compute INT_MIN / -1\n"); fflush(stdout);
    printf("result: %d\n", x / y);               /* UB: the quotient 2^31 does not fit */
    return 0;
}
```

```
about to compute INT_MIN / -1
exit=136
```

Exit status 136 is 128 + 8: the process died from signal 8, `SIGFPE`. On ARM64 the `sdiv` instruction does not trap and returns a wrapped value instead (from memory; not measured here), so this crash is **x86-specific**: another reason a UB-triggering program "working" on your Mac proves nothing about Linux on x86. UBSan reports it identically on both:

```
about to compute INT_MIN / -1
s/divide.c:8:30: runtime error: division of -2147483648 by -1 cannot be represented in type 'int'
exit=1
```

**Sequence points.** The standard (C11) says the order in which operands are evaluated is mostly **unspecified**, and that two unsequenced side effects on the same object are UB. `i = i++ + 1;` modifies `i` twice (once by `++`, once by the assignment) with nothing ordering the two. GCC warns:

```
s/sequence.c: In function 'main':
s/sequence.c:7:7: warning: operation on 'i' may be undefined [-Wsequence-point]
    7 |     i = i++ + 1;                                 /* two unsequenced modifications of i: UB */
      |     ~~^~~~~~~~~
```

```c
#include <stdio.h>
static int counter = 0;
static int next(void) { return ++counter; }
int main(void) {
    printf("%d %d\n", next(), next());           /* order of evaluation: unspecified */
    int i = 1;
    i = i++ + 1;                                 /* two unsequenced modifications of i: UB */
    printf("i = %d\n", i);
    return 0;
}
```

```
2 1
i = 2
exit=0
```

Two lessons from one program. First, `next()` twice in one `printf` printed `2 1`: this GCC evaluated the arguments right to left. Another compiler, or another version, may print `1 2`, and both are conforming (unspecified behaviour). Second, `i = 2` is one possible answer to the UB line, and the compiler is not obliged to give it again. Never write code whose result depends on either. The fix is always to split into separate statements.

**Strict aliasing.** The compiler may assume that a pointer to `int` and a pointer to `float` never point to the same memory. So after `*i = 1; *f = 2.0f;` it may return `1` without reloading `*i`:

```c
#include <stdio.h>
#include <string.h>
int bump(int *i, float *f) {
    *i = 1;
    *f = 2.0f;                                   /* the compiler may assume this cannot change *i */
    return *i;
}
int main(void) {
    union { int i; float f; } u;
    printf("bump through the same memory: %d\n", bump(&u.i, &u.f));
    float f = 1.0f; unsigned bits;
    memcpy(&bits, &f, sizeof bits);              /* the portable way to read the bits of a float */
    printf("bits of 1.0f: 0x%08x\n", bits);
    return 0;
}
```

At `-O0` the program prints the bits of 2.0f read back as an int; at `-O2` it prints 1:

```
bump through the same memory: 1073741824
bits of 1.0f: 0x3f800000
exit=0
```

```
bump through the same memory: 1
bits of 1.0f: 0x3f800000
exit=0
```

Both are "right" because the program is wrong. UBSan does not detect aliasing (the `-fsanitize=undefined` run printed the `-O0`-like result), so you find it by compiling with `-fno-strict-aliasing` and seeing if the behaviour changes. The three **legal** ways to look at an object's bytes are: access through `char`/`unsigned char`, `memcpy` (the compiler turns it into a register move, so it is free; the second line of output is exactly that), and reading a different member of a `union` (allowed in C, not in C++).

**Null checks after the dereference.** If you use a pointer, the compiler concludes it is not null:

```c
#include <stdio.h>
int g(int *p) {
    int v = *p;                                  /* dereference first ... */
    if (p == NULL) puts("p is null");            /* ... then check */
    (void)v;
    return 5;
}
int main(void) {
    printf("g(NULL) returned %d\n", g(NULL));
    return 0;
}
```

At `-O0`:

```
exit=139
```

At `-O2`:

```
g(NULL) returned 5
exit=0
```

At `-O0` the load from address 0 crashes (exit status 139 is 128 + 11, `SIGSEGV`). At `-O2` the compiler reasons "`*p` was executed, so `p` is not null, so the `if` is dead, and since `v` is unused the load is dead too", deletes both, and the program happily returns 5. Moving the check **before** the dereference fixes it. UBSan's `null` check catches it (`runtime error: load of null pointer of type 'int'`). A related real-world form of this bug is the Linux kernel null-pointer issue that led to `-fno-delete-null-pointer-checks` being used for kernel builds (from memory; not measured here).

## 6. Why "it works" proves nothing, and what the optimiser may do

The compiler works on a model of your program in which UB cannot happen. Everything below is legal for a program that has UB, because the standard requires nothing of it:

1. **Delete code that can only run if the UB happened**, such as the overflow check and the null check above.
2. **Assume values**: after `x + 1` it may treat `x` as not `INT_MAX`; after `a[i]` it may treat `i` as in range, and remove a later bounds check on `i`.
3. **Reorder or remove loads and stores** that would only matter if two pointers aliased.
4. **Act on UB "in the past"**: because the whole execution has no meaning, a statement that comes *before* the UB in program order can be affected. Printing a message and then dividing by zero may print nothing at all.
5. **Do the obvious thing at `-O0` and something else at `-O2`, or the reverse.** The two levels agreeing proves nothing, and the two levels disagreeing is the best clue you will get.

The practical consequence is a working method: when a snippet might contain UB, compile it at `-O0` and `-O2`, with `-fsanitize=address,undefined`, and with `-fwrapv` and `-fno-strict-aliasing` as probes. A difference between any two runs means "undefined" or "unspecified", and you have found where.

## 7. Writing code that stays inside the contract

Flags to build with while developing:

| Flag | What it gives you |
|---|---|
| `-Wall -Wextra` | the usual warnings, including `-Wsign-compare` and `-Wsequence-point` |
| `-Wconversion -Wsign-conversion` | every implicit narrowing or signedness change; noisy, but worth one pass on new code |
| `-fsanitize=address,undefined` | overflow, shifts, null, bounds, alignment, use-after-free at run time (slows by about 2x) |
| `-fwrapv` | defines signed overflow (changes the language; use only if you mean it) |
| `-ftrapv` or `-fsanitize=signed-integer-overflow` | turn signed overflow into a crash |

Habits that remove most of the problems:

- Use **`unsigned` types for bit manipulation and hashing** (where wraparound is the point) and **signed types for quantities** that can be negative or compared with a subtraction.
- Do not mix signed and unsigned in one expression; cast **once, deliberately**, at the boundary.
- Use `size_t` for sizes and counts, but write reverse loops as `for (size_t i = n; i-- > 0; )`.
- **Check the operands before the operation**, never the result: `if (b > 0 && a > INT_MAX - b)` instead of `if (a + b < a)`.
- Use `1u << k` (or `UINT32_C(1) << k`) when forming bit masks, and keep `k` in `0..width-1`.
- Use `memcpy` for type punning and for reading integers out of byte buffers.
- Prefer the compiler's checked arithmetic where available: `__builtin_add_overflow`, `__builtin_sub_overflow`, `__builtin_mul_overflow` (GCC and Clang), standardised as `ckd_add` and friends in C23 (from memory).

```c
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
/* Portable test: would a + b overflow? Never computes the overflowing sum. */
static bool add_overflows(int a, int b) { return (b > 0 && a > INT_MAX - b) || (b < 0 && a < INT_MIN - b); }
/* Reverse the decimal digits of x; 0 if the result does not fit (LeetCode 7). */
static int reverse_int(int x) {
    int r = 0;
    while (x != 0) {
        int d = x % 10; x /= 10;
        if (r > INT_MAX / 10 || (r == INT_MAX / 10 && d > 7)) return 0;
        if (r < INT_MIN / 10 || (r == INT_MIN / 10 && d < -8)) return 0;
        r = r * 10 + d;
    }
    return r;
}
int main(void) {
    printf("add_overflows(INT_MAX, 1) = %d\n", add_overflows(INT_MAX, 1));
    printf("add_overflows(INT_MIN, -1) = %d\n", add_overflows(INT_MIN, -1));
    printf("add_overflows(100, -200) = %d\n", add_overflows(100, -200));
    int sum;
    bool bad = __builtin_add_overflow(INT_MAX, 1, &sum);
    printf("__builtin_add_overflow(INT_MAX, 1) = %d, wrapped sum %d\n", bad, sum);
    printf("reverse_int(123) = %d, reverse_int(-120) = %d, reverse_int(1534236469) = %d\n", reverse_int(123), reverse_int(-120), reverse_int(1534236469));
    return 0;
}
```

```
add_overflows(INT_MAX, 1) = 1
add_overflows(INT_MIN, -1) = 1
add_overflows(100, -200) = 0
__builtin_add_overflow(INT_MAX, 1) = 1, wrapped sum -2147483648
reverse_int(123) = 321, reverse_int(-120) = -21, reverse_int(1534236469) = 0
exit=0
```

`add_overflows` never forms the overflowing sum, so it is free of UB at every optimisation level. The builtin gives both the flag and the wrapped result. `reverse_int` (the LeetCode "reverse integer" problem) checks `r` against `INT_MAX / 10` **before** multiplying by 10, and the last digit against 7 (the last digit of 2147483647) or -8, which is why `1534236469` (whose reverse, 9646324351, does not fit) yields 0.

## 8. Recognising the problem

| You see | The issue | The fix |
|---|---|---|
| `if (a + b < a)` on `int` | overflow check after the UB; deleted at `-O2` | `if (b > 0 && a > INT_MAX - b)` or the builtin |
| `for (size_t i = n - 1; i >= 0; --i)` | `i` is unsigned, so the test is always true | `for (size_t i = n; i-- > 0; )` |
| `if (x < sizeof buf)` with `int x` | `x` converted to `size_t`; a negative `x` passes as huge | compare as `size_t` after checking `x >= 0` |
| `long long r = a * b;` with `int` | multiplied in `int`, then widened | `(long long)a * b` |
| `1 << k` with `k` up to 31 or more | UB at 31 for `int`; UB at 32 or more | `1u << k` with `k < 32`, or `UINT64_C(1) << k` |
| `char c = getchar();` then `c == EOF` | `char` may be unsigned, so EOF (-1) never matches | `int c = getchar();` |
| `abs(x)` on any `int` | UB for `INT_MIN` | widen first, or handle `INT_MIN` |
| `uint8_t x; x << 8` | `x` promoted to `int` first; the high bits survive | cast the result back if you want it truncated |
| `memcpy`-free `*(uint32_t *)buf` | aliasing and alignment UB | `memcpy(&v, buf, 4)` |
| `i = i++ + 1`, `a[i] = i++`, `f(i++, i++)` | unsequenced / unspecified | one side effect per statement |

## 9. Common mistakes

1. **Believing `-O0` is "what the code means".** UB has no meaning at any level; GCC folded `x + 1 > x` even at `-O0`.
2. **Checking for overflow after it happened** (`a + b < a`).
3. **Using `int` for a size or index** and mixing it with `size_t`.
4. **Mixing signed and unsigned in a comparison** and trusting the warning is harmless.
5. **Forgetting that small types are promoted:** `~uint8_t`, `uint8_t << n` and `uint16_t * uint16_t` are computed in `int`, and the last one can overflow `int` (65535 * 65535 > INT_MAX, UB).
6. **Assuming plain `char` is signed** (or unsigned).
7. **Shifting `1` instead of `1u`,** or shifting by the width.
8. **`abs(INT_MIN)` and `-INT_MIN`.**
9. **Assuming a right shift of a negative value is division by two** for odd numbers: it rounds toward minus infinity, while `/` truncates toward zero (`-7 >> 1` is -4, `-7 / 2` is -3).
10. **Writing a null check after the dereference.**
11. **Casting a pointer to read other bytes** instead of `memcpy` or a `char` pointer.
12. **Relying on argument evaluation order,** or on a UB result that "happens to be stable".
13. **Testing only on x86** (or only on ARM): the crash on `INT_MIN / -1`, the signedness of `char` and the behaviour of oversize shifts all differ.

## 10. Questions to answer out loud

1. What are the guaranteed minimum widths of `char`, `short`, `int`, `long` and `long long`? What is `sizeof(char)`?
2. What is the difference between implementation-defined, unspecified and undefined behaviour? Give one example of each.
3. Walk through integer promotion for `unsigned char a = 200, b = 100; a + b`.
4. Why is `-1 < 1u` false? State the rule that causes it.
5. Why is signed overflow undefined while unsigned overflow wraps? What does that let the compiler do?
6. Why can `if (x + 1 < x)` be deleted by the compiler? How do you write the check correctly?
7. Why is `i = i++ + 1;` undefined? What does "unsequenced" mean?
8. What does `-fwrapv` do? What does `-fsanitize=undefined` do? Which would you ship?
9. Why can `INT_MIN / -1` crash on x86 and not on ARM? Is that a hardware bug?
10. What is strict aliasing, and what are the legal ways to read a `float` as its bits?
11. Why can a null check written after a dereference be removed?
12. A program works at `-O0` and fails at `-O2`. What are the first three things you test?

## 11. Problems

### 11.1 On paper

For each snippet say whether it is **defined**, **implementation-defined**, **unspecified** or **undefined**, and give the value when it is defined. Assume GCC on x86-64 Linux, `int` is 32 bits. Run each one after you answer.

**A.** `unsigned char x = 255; x++; printf("%d", x);`

**B.** `short s = -1; unsigned short us = s; printf("%d", us);`

**C.** `int a = 5; unsigned b = 10; printf("%d", a - b > 0);`

**D.** `printf("%d %d %d %d", 7 / 2, -7 / 2, 7 % 2, -7 % 2);`

**E.** `int x = INT_MAX; x += 1;`

**F.** `uint8_t f = 0xF0; printf("%x %x", f << 4, (uint8_t)(f << 4));`

**G.** `size_t n = 3; for (size_t i = n; i >= 0; --i) { ... }` What happens, and how do you fix it?

**H.** `char c = 'A'; printf("%zu %zu", sizeof c, sizeof 'A');`

**I.** `int k = -8; printf("%d %u", k >> 1, (unsigned)k >> 1);`

**J.** `printf("%d", (int)(unsigned char)-1);`

### 11.2 On LeetCode

Solve these in C. For each, say which expression can overflow and how you avoid it before you write code. Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | What to practise |
|---|---|---|
| Reverse Integer | 7 | The overflow check before multiplying (section 7) |
| String to Integer (atoi) | 8 | Accumulating a value without overflow, saturating at `INT_MAX` / `INT_MIN` |
| Divide Two Integers | 29 | `INT_MIN / -1`, and avoiding `/` and `*` using shifts on a wider type |
| Number of 1 Bits | 191 | `unsigned` and `x & (x - 1)` |
| Reverse Bits | 190 | Shifts on `uint32_t`, never on a signed type |
| Sum of Two Integers | 371 | Adding with XOR and carry; do the carry shift on `unsigned` |

### 11.3 In your editor

**A.** Write `int saturating_add(int a, int b)` that returns `INT_MAX` or `INT_MIN` instead of overflowing, using only the "check before" idiom. Test it against `__builtin_add_overflow` on 10 million random pairs.

**B.** Write `unsigned abs_u(int x)` that is correct for `INT_MIN` (hint: negate in unsigned arithmetic) and print its result for `INT_MIN`.

**C.** Take each program from this tutorial and run it at `-O0`, `-O1`, `-O2`, `-O3`, with `-fsanitize=undefined`, with `-fwrapv`, and with `-fno-strict-aliasing`. Make a table of which runs differ. This is the habit the whole track relies on.

**D.** Print `sizeof` and the limit macros of every integer type on your Mac and on this tutorial's reference output, and list every difference. (Expected: none for the types in section 1, a difference in the signedness of `char` only if you cross-compile.)

**E.** Write a function `int mul_checked(int a, int b, int *out)` without the builtin, using a `long long` product when `long long` is wider than `int`, and without it (using division) when it is not.

### Answers

**A.** Defined: `x++` is computed as int 256 and converted back modulo 256. Prints `0`.

**B.** Defined: converting -1 to `unsigned short` gives 65535. Prints `65535`.

**C.** Defined: `a - b` is an `unsigned`, 4294967291, greater than 0. Prints `1`.

**D.** Defined since C99: `3 -3 1 -1`.

**E.** Undefined behaviour (signed overflow). With `-fwrapv` it would be defined and give `INT_MIN`.

**F.** Defined: `f` is promoted to `int`, so `f << 4` is 0xF00, and the cast to `uint8_t` keeps the low 8 bits. Prints `f00 0`.

**G.** Defined but wrong: an unsigned `i` is always `>= 0`, so the loop never stops and `i` wraps from 0 to `SIZE_MAX`. Fix: `for (size_t i = n; i-- > 0; )` (body sees `n - 1` down to 0).

**H.** Defined. A character constant has type `int` in C (it is `char` in C++), so this prints `1 4`.

**I.** `k >> 1` is implementation-defined (arithmetic shift here, so `-4`); the unsigned shift is defined: `-8` as `unsigned` is 4294967288, halved gives `2147483644`. Prints `-4 2147483644`.

**J.** Defined: `-1` converted to `unsigned char` is 255. Prints `255`.

A program that checks every defined case (E is left out because it is undefined; G is in its fixed form) printed:

```
A 0
B 65535
C 1
D 3 -3 1 -1
F f00 0
G 4
H 1 4
I -4 2147483644
J 255
```

## Cheat sheet

- **Sizes are minimums:** `char` 8, `short` 16, `int` 16, `long` 32, `long long` 64 bits; use `<stdint.h>` when the width matters. `sizeof(char)` is 1.
- **Plain `char` signedness is implementation-defined;** use `unsigned char` or `uint8_t` for bytes.
- **Promotion:** everything smaller than `int` becomes `int` before arithmetic. **Usual conversions:** mixing signed and unsigned of the same rank makes the signed value unsigned.
- **To unsigned:** reduce modulo `2^N` (defined). **To signed, out of range:** implementation-defined.
- **Unsigned overflow wraps** (defined). **Signed overflow is UB.** `-INT_MIN`, `abs(INT_MIN)`, `INT_MIN / -1` are UB.
- **Shifts:** count must be `0 .. width-1`; left shift of a negative value is UB; right shift of a negative is implementation-defined; form masks with `1u`.
- **Division** truncates toward zero; `%` has the sign of the dividend.
- **Behaviours:** defined, implementation-defined (documented), unspecified (any of a set, undocumented), undefined (no requirement).
- **UB catalogue:** overflow, shifts, divide, out of bounds, null, use after free, uninitialised read, unsequenced side effects, strict aliasing, misalignment, writing literals, falling off the end of a function.
- **Optimiser:** assumes UB never happens; deletes checks written after the fact; `-O0` is no safe harbour.
- **Tools:** `-Wall -Wextra -Wconversion`, `-fsanitize=address,undefined`, compare `-O0` with `-O2`, probes `-fwrapv` and `-fno-strict-aliasing`.
- **Check before you compute:** `a > INT_MAX - b`, or `__builtin_add_overflow`.

---

## Sources

- All outputs are measured here (GCC 13.3, `-std=c11`, x86-64 Linux, LP64, one run each; the programs are deterministic). Statements about ARM64, Windows, 32-bit targets, Clang, C23 and kernel build flags are marked "from memory" or "not measured here" and should be checked on your own machine.
- The rules for integer promotion, the usual arithmetic conversions, the four classes of behaviour, sequencing and strict aliasing are from the C standard (C11, the sections on conversions, expressions and the list of undefined behaviour); the wording and examples here are my own. For the exact text, read the C11 draft (N1570) sections 6.3 (conversions), 6.5 (expressions) and Annex J.2 (the undefined-behaviour list).
- Problem numbers and titles in section 11.2 are LeetCode's, from memory; check a number before you rely on it.

