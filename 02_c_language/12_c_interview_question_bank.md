# C 12: The C Interview Question Bank

*Part of the [C language plan](00_plan.md) · Time: about 8 hours (5 reading and drills, 3 problems) · You need: [C 01](01_types_conversions_and_undefined_behavior.md) to [C 11](11_robust_and_secure_c.md); this tutorial tests them and adds the questions that no single earlier chapter covers*

This is the revision chapter of the track. It has 48 questions in the two shapes C interviews use ("what does this print" and "what is wrong here"), three implementations that interviewers ask for at the whiteboard (a ring buffer, linked-list operations and a fixed-block allocator, each with a test suite and mutants), and the embedded-C questions that platform, firmware and driver roles add (`volatile`, memory-mapped registers, interrupt handlers, watchdogs). Every program was compiled and run; the answers are the measured output, and where a result depends on the compiler or on the machine, the text says so. The earlier chapters hold the theory; here the skill is answering fast and with the reason.

By the end you can:

- classify any snippet as defined, unspecified, implementation-defined or undefined, and answer "what does this print" with the output and the rule, or with "undefined, and here is why" when that is the correct answer;
- predict the results of integer promotions, signed and unsigned comparisons, `sizeof` on arrays and pointers, pointer arithmetic, macro expansion, `static` locals, `switch` fall-through and Duff's device;
- spot the bug in a short snippet (dangling pointer, off-by-one allocation, `sizeof` on a parameter, `char` against `EOF`, overlapping `memcpy`, a data race, `memcmp` on a struct, use after free in a free loop) and name the tool that reports it;
- write from memory, test, and defend a ring buffer (with the free-running counter trick and a lock-free single-producer, single-consumer version), the standard linked-list operations (reverse, cycle test, middle, remove the n-th from the end, stable merge) and a fixed-block allocator that detects double frees;
- explain `volatile` with a program that hangs without it, write a register-level driver against a device model, write an interrupt handler that obeys the rules, explain write-1-to-clear registers, and design a watchdog that detects a hung task.

**Where the numbers come from.** Every program and command was run on an x86-64 Linux container (Ubuntu, glibc 2.39, GCC 13.3, clang 18.1.3, 2 vCPUs). Code is compiled with `-std=gnu11 -Wall -Wextra -Wno-unused-function` unless the text says otherwise; the question programs are compiled with `-w` (no warnings) when the point is the output, and with `-Wall -Wextra` when the point is the diagnostic. The library of section 5 (ring buffer, list, pool) and section 6 (UART and watchdog) is in two headers, `ds.h` and `emb.h`, printed in those sections. Its test program makes 11,226,979 checks at its default setting and runs clean under `-fsanitize=address,undefined`; it was also run against 30 deliberate defects (section 5.4). The threaded programs were checked with ThreadSanitizer. I did not measure other compilers or other architectures except where the text names clang; statements about ARM, AVR and 8-bit targets are from memory and are marked.

**Data and timing.** Machine: a cloud container, Intel Xeon 2.10 GHz, 2 vCPUs, GCC 13.3.0, `-O2`. Timings are the best of 3 wall-clock runs in milliseconds, shown as run 1 / run 2 (two complete repetitions of the whole benchmark). Output of undefined behaviour is what this compiler produced and is not a promise.

---

## 1. How to answer a "what does this print" question

Interviewers use these questions to test whether you know what the language promises. The answer they want is the output **and** the rule. A reliable routine, in order:

1. **Is any of it undefined?** Signed overflow, two unsequenced modifications of one object, writing to a string literal or a `const` object, an out-of-range shift, reading an uninitialised variable, a pointer past one-past-the-end. If yes, say "undefined", name the rule, and say what you would expect on this compiler if asked. Do not invent a "correct" output.
2. **Is any of it unspecified or implementation-defined?** The order in which function arguments are evaluated is unspecified; whether `char` is signed, the result of converting an out-of-range value to a signed type and the size of `long` are implementation-defined. Say which, and give the answer for x86-64 Linux with GCC.
3. **Types.** Apply the integer promotions (anything narrower than `int` becomes `int`) and the usual arithmetic conversions (if the types still differ and the unsigned one is at least as wide, the signed operand becomes unsigned). Most surprises in this chapter are here.
4. **Sizes.** `sizeof` an array is the whole array; a function parameter declared as an array is a pointer.
5. **Lifetimes and storage.** `static` persists, a local does not, a string literal is read-only.
6. **Then evaluate, left to right where the standard says so**: `&&`, `||`, `?:` and the comma operator sequence their operands; function arguments and most other operators do not.

| Class | Meaning | Example in this chapter |
|---|---|---|
| Defined | one result, on every conforming compiler | Q2, Q5, Q18 |
| Implementation-defined | the compiler must document the choice | Q10: `char` signedness and the out-of-range conversion |
| Unspecified | one of several results, no documentation needed | Q28: the order of the arguments of a call |
| Undefined | anything may happen | Q7, Q11, Q30 |

The rules for the evaluation order and for sequence points are on cppreference's page on evaluation order (checked: argument order is unspecified, two unsequenced modifications of one scalar are undefined, and `&&`, `||`, `,` and `?:` have a sequence point after their first operand).

---

## 2. Part A: what does this print? (36 questions)

Cover the answers. For each snippet, write down the output before you read section 3. All snippets are complete programs; the includes are shown.

### Types and conversions

**Q1.**

```c
#include <stdio.h>

int main(void)
{
    printf("%d\n", -1 < 0u);
    printf("%d\n", -1 < 0);
    return 0;
}
```

**Q2.**

```c
#include <stdio.h>

int main(void)
{
    unsigned char a = 200, b = 100;
    printf("%d %d\n", a + b, (unsigned char)(a + b));
    return 0;
}
```

**Q3.**

```c
#include <stdio.h>

int main(void)
{
    short s = -1;
    unsigned u = s;
    printf("%u %x\n", u, u);
    return 0;
}
```

**Q4.**

```c
#include <stdio.h>

int main(void)
{
    printf("%zu %zu %zu\n", sizeof('a'), sizeof(char), sizeof("abc"));
    return 0;
}
```

**Q5.**

```c
#include <stdio.h>

int main(void)
{
    printf("%d %d %d %d\n", 7 / 2, -7 / 2, -7 % 2, 7 % -2);
    return 0;
}
```

**Q6.**

```c
#include <stdio.h>

int main(void)
{
    float f = 0.1f;
    printf("%d %d\n", f == 0.1, f == 0.1f);
    return 0;
}
```

**Q7.**

```c
#include <limits.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    (void)argv;
    int i = INT_MAX - 1 + argc;      /* INT_MAX at run time */
    printf("%d\n", i + 1 > i);
    return 0;
}
```

**Q8.**

```c
#include <stdio.h>

int main(void)
{
    unsigned u = 0;
    printf("%u %d\n", u - 1, u - 1 > 0);
    return 0;
}
```

**Q9.**

```c
#include <stdio.h>

int main(void)
{
    double d = 3.99;
    printf("%d %d %d\n", (int)d, (int)-d, (int)(d + 0.5));
    return 0;
}
```

**Q10.**

```c
#include <stdio.h>

int main(void)
{
    char c = 200;
    printf("%d %d\n", c, (char)0x80 == 0x80);
    return 0;
}
```

### Operators and evaluation order

**Q11.**

```c
#include <stdio.h>

int main(void)
{
    int i = 5;
    int r = i++ + ++i;
    printf("%d %d\n", r, i);
    return 0;
}
```

**Q12.**

```c
#include <stdio.h>

int main(void)
{
    int a = 1, b = 2;
    int c = (a++, b++, a + b);
    int d = (a, b);
    printf("%d %d %d %d\n", a, b, c, d);
    return 0;
}
```

**Q13.**

```c
#include <stdio.h>

int main(void)
{
    int x = 0, y = 0;
    if (x++ && x++)
        y = 1;
    int z = 0;
    if (x++ || x++)
        z = x;
    printf("%d %d %d\n", x, y, z);
    return 0;
}
```

**Q14.**

```c
#include <stdio.h>

int main(void)
{
    printf("%d %d %d\n", 2 + 3 << 1, 2 + (3 << 1), 1 + 2 & 3);
    printf("%d\n", !!5 + !0 + ~0);
    return 0;
}
```

**Q15.**

```c
#include <stdio.h>

int main(void)
{
    printf("%d\n", printf("ab"));
    return 0;
}
```

### Arrays, pointers, strings and layout

**Q16.**

```c
#include <stdio.h>

static void f(int a[10], int b[], int *c)
{
    printf("%zu %zu %zu\n", sizeof a, sizeof b, sizeof c);
}

int main(void)
{
    int a[10];
    printf("%zu %zu\n", sizeof a, sizeof a / sizeof a[0]);
    f(a, a, a);
    return 0;
}
```

**Q17.**

```c
#include <stdio.h>

int main(void)
{
    int a[5] = {10, 20, 30, 40, 50};
    int *p = a + 1;
    printf("%d %d %d %td\n", *p, p[2], *(p - 1), &a[4] - p);
    return 0;
}
```

**Q18.**

```c
#include <stdio.h>

int main(void)
{
    int a[4];
    printf("%td %td\n", (char *)(&a + 1) - (char *)a, (char *)(a + 1) - (char *)a);
    return 0;
}
```

**Q19.**

```c
#include <stdio.h>

int main(void)
{
    char s[] = "hello";
    char *p = "hello";
    printf("%zu %zu %zu\n", sizeof s, sizeof p, sizeof "hello");
    s[0] = 'J';
    printf("%s\n", s);
    return 0;
}
```

**Q20.**

```c
#include <stdio.h>
#include <string.h>

int main(void)
{
    printf("%zu %zu\n", strlen("abc\0def"), sizeof "abc\0def");
    char a[] = "hi", b[] = "hi";
    printf("%d %d\n", a == b, strcmp(a, b) == 0);
    return 0;
}
```

**Q21.** (run it: what happens, and what is the exit status?)

```c
#include <stdio.h>

int main(void)
{
    char *p = "abc";
    printf("before\n");
    fflush(stdout);
    p[0] = 'x';
    printf("after %s\n", p);
    return 0;
}
```

**Q22.**

```c
#include <stdio.h>

int main(void)
{
    int m[2][3] = {{1, 2, 3}, {4, 5, 6}};
    printf("%d %d %zu %zu\n", *(*(m + 1) + 2), (*m)[1], sizeof m[0], sizeof m);
    return 0;
}
```

**Q23.**

```c
#include <stdio.h>

int main(void)
{
    printf("%s %c %c\n", "abc" + 1, "abc"[2], 2["abc"]);
    int a[4] = {1, 2};
    printf("%d %d %d\n", a[1], a[2], a[3]);
    return 0;
}
```

**Q24.**

```c
#include <stdio.h>

struct A { char a; int b; char c; };
struct B { int b; char a; char c; };
struct C { char a; int b; char c; } __attribute__((packed));

int main(void)
{
    printf("%zu %zu %zu\n", sizeof(struct A), sizeof(struct B), sizeof(struct C));
    return 0;
}
```

**Q25.**

```c
#include <stdio.h>

int main(void)
{
    union { float f; unsigned u; } x = { 1.0f };
    unsigned one = 1;
    printf("%#x %d\n", x.u, *(unsigned char *)&one);
    return 0;
}
```

### Preprocessor, storage and qualifiers

**Q26.**

```c
#include <stdio.h>

#define SQ(x) x * x
#define SQ2(x) ((x) * (x))

int main(void)
{
    printf("%d %d\n", SQ(1 + 2), SQ2(1 + 2));
    printf("%d %d\n", 36 / SQ(3), 36 / SQ2(3));
    return 0;
}
```

**Q27.**

```c
#include <stdio.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))

int main(void)
{
    int i = 5, j = 3;
    int m = MAX(i++, j++);
    printf("%d %d %d\n", m, i, j);
    return 0;
}
```

**Q28.**

```c
#include <stdio.h>

static int counter(void)
{
    static int n;
    return ++n;
}

static int plain(void)
{
    int n = 0;
    return ++n;
}

int main(void)
{
    printf("%d %d %d\n", counter(), counter(), counter());
    printf("%d %d %d\n", plain(), plain(), plain());
    return 0;
}
```

**Q29.**

```c
#include <stdio.h>

#define S(x) #x
#define XS(x) S(x)
#define N 42
#define CAT(a, b) a##b

int main(void)
{
    int xy = 7;
    printf("%s %s %d\n", S(N), XS(N), CAT(x, y));
    return 0;
}
```

**Q30.**

```c
#include <stdio.h>

int main(void)
{
    const int c = 5;
    int *p = (int *)&c;
    *p = 6;
    printf("%d %d\n", c, *p);
    return 0;
}
```

**Q31.**

```c
#include <stdio.h>

int main(void)
{
    int i = 0;
    size_t n = sizeof(i++);
    printf("%d %zu\n", i, n);
    return 0;
}
```

**Q32.**

```c
#include <stdio.h>

int main(void)
{
    struct { unsigned a : 3; } s = { 7 };
    s.a++;
    printf("%u\n", s.a);
    unsigned char c = 255;
    c++;
    printf("%d\n", c);
    return 0;
}
```

### Control flow and Duff's device

**Q33.** Duff's device. What does each line print, and why does it work?

```c
#include <stdio.h>

/* Duff's device: copy n bytes (n > 0) with the loop unrolled by 8. */
static void duff_copy(char *to, const char *from, int n)
{
    int rounds = (n + 7) / 8;
    switch (n % 8) {
    case 0: do { *to++ = *from++;
    case 7:      *to++ = *from++;
    case 6:      *to++ = *from++;
    case 5:      *to++ = *from++;
    case 4:      *to++ = *from++;
    case 3:      *to++ = *from++;
    case 2:      *to++ = *from++;
    case 1:      *to++ = *from++;
            } while (--rounds > 0);
    }
}

int main(void)
{
    const char *src = "ABCDEFGHIJKLMNOPQRST";
    for (int n = 1; n <= 17; n += 4) {
        char dst[32] = {0};
        duff_copy(dst, src, n);
        printf("n=%2d \"%s\"\n", n, dst);
    }
    return 0;
}
```

**Q34.**

```c
#include <stdio.h>

int main(void)
{
    for (int k = 0; k < 4; ++k) {
        switch (k) {
        case 1: printf("one ");
        case 2: printf("two ");
                break;
        case 3: printf("three ");
        default: printf("other ");
        }
        printf("| ");
    }
    printf("\n");
    return 0;
}
```

**Q35.**

```c
#include <stdio.h>

int main(void)
{
    int shown = 0;
    for (unsigned i = 3; i >= 0; i--) {
        printf("%u ", i);
        if (++shown == 6)
            break;
    }
    printf("\n");
    int j = 3;
    while (j --> 0)
        printf("%d ", j);
    printf("\n");
    return 0;
}
```

**Q36.**

```c
#include <stdio.h>

static int f(int x) { return x ? x : 99; }

int main(void)
{
    int a[3] = {0};
    int i = 0;
    a[i] = f(i);
    i = 1;
    a[i] = f(i);
    printf("%d %d %d\n", a[0], a[1], a[2]);
    printf("%d\n", sizeof(int) > -1);
    return 0;
}
```

---

## 3. Part A: the answers

Each answer is the measured output, then the rule.

**Q1.** `0 / 1`. In `-1 < 0u` the `int` is converted to `unsigned` (usual arithmetic conversions), becoming 4294967295, which is not less than 0. `-Wextra` warns about it (`-Wsign-compare`).

**Q2.** `300 44`. Both `unsigned char` operands are promoted to `int`, so the sum is 300; the cast keeps the low 8 bits: 300 mod 256 = 44.

**Q3.** `4294967295 ffffffff`. The `short` -1 is converted to `unsigned int` by value: -1 modulo 2^32.

**Q4.** `4 1 4`. A character constant has type `int` in C (it is 1 in C++), so `sizeof('a')` is 4. `sizeof "abc"` is 4: the array includes the terminator.

**Q5.** `3 -3 -1 1`. Since C99, integer division truncates toward zero, and the remainder takes the sign of the dividend, so that `(a / b) * b + a % b == a`.

**Q6.** `0 1`. `0.1f` widened to `double` is 0.10000000149..., not 0.1. Compare floating-point values with a tolerance, and watch the types of the constants.

**Q7.** With GCC, `-O0: 1 / -O2: 1` (the first is `-O0`, the second `-O2`). Signed overflow is **undefined**, so the compiler may fold `i + 1 > i` to true, and GCC does it even at `-O0` here. On a machine that wraps, the answer would be 0. The same programs built with clang 18.1.3, next to the GCC results (the first two lines are Q28, below):

```text
q28 with gcc 13.3:   3 2 1 / 1 1 1
q28 with clang 18.1: 1 2 3 / 1 1 1
q07 with gcc -O0 and -O2: 1 and 1
q07 with clang -O0 and -O2: 0 and 1
```

The interview answer is "undefined; the output depends on the optimiser, and the portable test is `i < INT_MAX` before the addition".

**Q8.** `4294967295 1`. `u - 1` wraps to 4294967295 (unsigned arithmetic is defined modulo 2^N), which is greater than 0.

**Q9.** `3 -3 4`. A cast from floating point to integer truncates toward zero (3.99 gives 3, -3.99 gives -3). Adding 0.5 before the cast rounds to nearest for positive values.

**Q10.** `-56 0`. Whether plain `char` is signed is implementation-defined; on x86-64 Linux with GCC it is signed, so 200 becomes -56 (the out-of-range conversion is implementation-defined too). `(char)0x80` is -128, which is not equal to 128. On ARM Linux `char` is unsigned by default, so the answers differ there (from memory, not measured).

**Q11.** `-O0: 12 7 / -O2: 12 7` on both optimisation levels here, but the code is **undefined**: `i` is modified twice with no sequence point between the two modifications. Another compiler, or another version, may print something else. Say "undefined" and do not defend an output.

**Q12.** `2 3 5 3`. The comma operator has a sequence point after its left operand: `a` is 2 and `b` is 3 when `a + b` is evaluated, so `c` is 5; `d` is the value of the right operand, 3.

**Q13.** `2 0 2`. In `x++ && x++` the left operand is 0, so the right one is not evaluated: `x` is 1 and `y` stays 0. In `x++ || x++` the left operand is 1 (true), the right one is skipped, `x` becomes 2 and `z` is 2.

**Q14.** `10 8 3 / 1`. `+` binds tighter than `<<` and than `&`, so `2 + 3 << 1` is `(2 + 3) << 1` = 10 and `1 + 2 & 3` is `(1 + 2) & 3` = 3. In the second line `!!5` is 1, `!0` is 1 and `~0` is -1, so the sum is 1.

**Q15.** `ab2`. The arguments of the outer `printf` are evaluated first: the inner call prints `ab` and returns 2, which the outer call then prints.

**Q16.** `40 10 / 8 8 8`: 40 bytes and 10 elements in `main`, then 8, 8, 8 in the function. A parameter written as `int a[10]` or `int b[]` is a pointer; the 10 is ignored. This is why `sizeof` on an array parameter is a bug (W03).

**Q17.** `20 40 10 3`. With `p = a + 1`: `*p` is 20, `p[2]` is 40, `*(p - 1)` is 10, and the difference of two pointers into the same array is in elements: 3.

**Q18.** `16 4`. `&a + 1` moves past the whole array (16 bytes); `a + 1` moves one element (4 bytes). The two pointers have different types, which is why the arithmetic differs.

**Q19.** `6 8 6 / Jello`. `s` is an array of 6 bytes (5 characters and the terminator), `p` is a pointer (8 bytes), `sizeof "hello"` is 6. `s` is a writable copy, so `s[0] = 'J'` is fine and prints `Jello`.

**Q20.** `3 8 / 0 1`. `strlen` stops at the first NUL (3); `sizeof` counts the whole literal including the second string and the terminator (8). `a == b` compares two different array addresses (0); `strcmp` compares the contents (1).

**Q21.**

```text
before
exit status 139
```

`before` is printed, the write to the string literal faults (the literal is in read-only memory), and the shell reports status 139 (128 + signal 11, SIGSEGV). Writing to a string literal is undefined; this is how it usually shows.

**Q22.** `6 2 12 24`. `m + 1` is row 1, so `*(*(m + 1) + 2)` is `m[1][2]` = 6; `(*m)[1]` is `m[0][1]` = 2; a row is 12 bytes and the matrix is 24.

**Q23.** `bc c c / 2 0 0`. `"abc" + 1` points at `bc`; `"abc"[2]` and `2["abc"]` are both `'c'`, because `a[i]` means `*(a + i)`, which is symmetric. In the second line the initialiser `{1, 2}` sets the remaining elements to 0.

**Q24.** `12 8 6`. `struct A` is `char`, 3 bytes of padding, `int`, `char`, 3 bytes of tail padding = 12; putting the `int` first gives 8; `packed` removes all padding: 6 (and may cost unaligned accesses, C 04).

**Q25.** `0x3f800000 1`. The IEEE 754 single-precision bit pattern of 1.0 is `0x3f800000`. Reading a different union member than the one last written is allowed in C (it is not in C++). The second value is the first byte of the integer 1: 1 means little endian.

**Q26.** `5 9 / 36 4`. `SQ(1 + 2)` expands to `1 + 2 * 1 + 2` = 5; `SQ2` has parentheses: 9. `36 / SQ(3)` is `36 / 3 * 3` = 36; `36 / SQ2(3)` is 4. Parenthesise every macro parameter and the whole expression.

**Q27.** `6 7 4`. `MAX` evaluates the larger argument twice: `5 > 3` is true, so `i++` runs once in the comparison and again in the result: `m` is 6, `i` is 7, and `j` was incremented once (4). Use a function, or a statement expression that evaluates once (C 10).

**Q28.** The order in which the arguments of a call are evaluated is **unspecified**, and the compilers differ:

```text
3 2 1
1 1 1
```

GCC evaluated the three calls right to left (so the leftmost argument got 3), and clang left to right (the table under Q7). The second line is the same for both: a non-`static` local starts again at 0. The interview answer is "unspecified; I would not write it", and `static` makes the counter persist (3 calls give three different values).

**Q29.** `N 42 7`. An operand of `#` is not macro-expanded first, so `S(N)` is `"N"`; going through a second macro (`XS`) expands `N` before stringifying: `"42"`. `##` pastes tokens: `CAT(x, y)` is the identifier `xy`.

**Q30.** `-O0: 6 6
-O2: 5 6`. Modifying a `const` object through a cast is undefined. At `-O0` the store happens and both reads see 6; at `-O2` the compiler knew `c` is constant 5 and folded the read. The two reads of the same object disagree.

**Q31.** `0 4`. The operand of `sizeof` is not evaluated (unless it is a variable length array), so `i` stays 0, and `sizeof(i++)` is `sizeof(int)`.

**Q32.** `0 / 0`. An unsigned bit-field of 3 bits holds 0 to 7; incrementing 7 wraps to 0. `unsigned char` 255 plus 1 is computed as `int` 256 and converted back: 0.

**Q33.** Duff's device copies exactly `n` bytes for every `n`:

```text
n= 1 "A"
n= 5 "ABCDE"
n= 9 "ABCDEFGHI"
n=13 "ABCDEFGHIJKLM"
n=17 "ABCDEFGHIJKLMNOPQ"
```

The `switch` jumps into the middle of the `do ... while` loop. For `n % 8 == 3` it enters at `case 3`, copies 3 bytes, reaches the `while`, and from then on every pass copies 8. The legality of jumping into a loop body is what a `case` label really is: a label on a statement, nothing more. The loop was an optimisation for 1980s compilers; section 6 measures it on GCC 13.3, where it does not pay. Tom Duff's own note (checked below) says the point was to express general loop unrolling in C and that any such transformation has to be justified by measurement.

**Q34.** `other | one two | two | three other | `. `k = 0` has no matching case and takes `default`. `k = 1` prints `one`, falls into `case 2` (`two`) and stops at `break`. `k = 3` prints `three` and falls into `default`. Fall-through is the reason for `-Wimplicit-fallthrough` and the `[[fallthrough]]` or comment convention.

**Q35.**

```text
3 2 1 0 4294967295 4294967294
2 1 0
```

`unsigned i >= 0` is always true, so the first loop would never end; it printed 3 2 1 0 and then 4294967295, and the `break` stopped it. The second loop uses the "goes to" idiom: `j --> 0` is `j-- > 0`, which prints 2 1 0.

**Q36.**

```text
99 1 0
0
```

`f(0)` returns 99 and goes into `a[0]`; `f(1)` returns 1. The last line is the classic: `sizeof(int)` has type `size_t` (unsigned), the constant `-1` is converted to `size_t` (a huge number), so `4 > huge` is false: 0.

---

## 4. Part B: what is wrong here? (12 questions) and the answers

For each snippet, find the bug and name the tool or flag that reports it, then compare with the measured output. The code comes first; the answers follow in the next subsection.

**W01.**

```c
#include <stdio.h>

static char *name(void)
{
    char buf[16] = "prince";
    return buf;
}

int main(void)
{
    puts(name());
    return 0;
}
```

**W02.**

```c
#include <stdlib.h>
#include <string.h>

int main(void)
{
    const char *t = "hello";
    char *s = malloc(strlen(t));
    strcpy(s, t);
    free(s);
    return 0;
}
```

**W03.**

```c
#include <string.h>

static void clear(int a[])
{
    memset(a, 0, sizeof a);
}

int main(void)
{
    int v[8] = {1, 2, 3};
    clear(v);
    return v[0];
}
```

**W04.**

```c
#include <stdio.h>

int main(int argc, char **argv)
{
    char *s = argc > 1 ? argv[1] : "yes";
    if (s == "yes")
        puts("matched");
    else
        puts("no match");
    return 0;
}
```

**W05.** (input: the bytes `a`, `b`, 0xFF, `c`, `d`, newline)

```c
#include <stdio.h>

int main(void)
{
    char c;
    int n = 0;
    while ((c = getchar()) != EOF)
        n++;
    printf("%d characters read\n", n);
    return 0;
}
```

**W06.**

```c
#include <stdio.h>
#include <string.h>

int main(void)
{
    char a[16] = "abcdefgh";
    memcpy(a + 2, a, 6);                /* overlapping: undefined */
    printf("%s\n", a);
    char b[16] = "abcdefgh";
    memmove(b + 2, b, 6);
    printf("%s\n", b);
    return 0;
}
```

**W07.**

```c
#include <stddef.h>
#include <stdio.h>

static void backwards(const int *a, size_t n)
{
    for (size_t i = n - 1; i >= 0; i--)
        printf("%d ", a[i]);
}

int main(void)
{
    int v[3] = {1, 2, 3};
    backwards(v, 3);
    return 0;
}
```

**W08.**

```c
#include <stdio.h>

#define SWAP(a, b) tmp = a; a = b; b = tmp;

int main(void)
{
    int x = 1, y = 2, n = 0, tmp = 99;
    if (n > 0)
        SWAP(x, y)
    printf("%d %d\n", x, y);
    return 0;
}
```

**W09.**

```c
#include <stdio.h>

int main(void)
{
    int n;
    scanf("%d", n);
    int a[4];
    int *p = a;
    printf("%zu\n", sizeof(p) / sizeof(p[0]));
    return n;
}
```

**W10.**

```c
#include <pthread.h>
#include <stdio.h>

static long counter;

static void *work(void *arg)
{
    (void)arg;
    for (int i = 0; i < 1000000; ++i)
        counter++;
    return NULL;
}

int main(void)
{
    pthread_t t1, t2;
    pthread_create(&t1, NULL, work, NULL);
    pthread_create(&t2, NULL, work, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    printf("%s\n", counter == 2000000 ? "2000000: correct" : "lost updates");
    return 0;
}
```

**W11.**

```c
#include <stdio.h>
#include <string.h>

struct P { char c; int i; };

int main(void)
{
    struct P a, b;
    memset(&a, 0x00, sizeof a);
    memset(&b, 0xff, sizeof b);
    a.c = b.c = 'x';
    a.i = b.i = 42;
    printf("fields equal: %d, memcmp equal: %d\n", a.c == b.c && a.i == b.i, memcmp(&a, &b, sizeof a) == 0);
    return 0;
}
```

**W12.**

```c
#include <stdio.h>
#include <stdlib.h>

struct node { int v; struct node *next; };

int main(void)
{
    struct node *head = NULL;
    for (int i = 0; i < 3; ++i) {
        struct node *n = malloc(sizeof *n);
        n->v = i;
        n->next = head;
        head = n;
    }
    while (head) {
        free(head);
        head = head->next;
    }
    puts("done");
    return 0;
}
```

### The answers to Part B

**W01: returning the address of a local.** The array dies when the function returns. GCC says so at compile time:

```text
w01.c:6:12: warning: function returns address of local variable [-Wreturn-local-addr]
w01.c:11:5: warning: argument 1 null where non-null expected [-Wnonnull]
```

(The second warning is the compiler noticing that the dangling pointer was replaced by a null in the caller.) The fix is to let the caller supply the buffer, return a `static` buffer (not thread safe), or return `malloc`ed memory with the ownership documented (C 09).

**W02: `malloc(strlen(t))` forgets the terminator.** AddressSanitizer reports a write of 6 bytes at the first byte after a 5 byte block:

```text
ERROR: AddressSanitizer: heap-buffer-overflow on address ADDR at pc ADDR bp ADDR sp ADDR
WRITE of size 6 at ADDR thread T0
    #1 main w02.c:8
ADDR is located 0 bytes after 5-byte region [ADDR,ADDR)
    #1 main w02.c:7
SUMMARY: AddressSanitizer: heap-buffer-overflow asan_interceptors.cpp:563 in strcpy
```

Fix: `malloc(strlen(t) + 1)`, or `strdup`. This is the single most common C allocation bug.

**W03: `sizeof` of an array parameter.** The parameter is a pointer, so `memset` clears 8 bytes instead of 32. GCC reports both aspects:

```text
w03.c:5:25: warning: ‘sizeof’ on array function parameter ‘a’ will return size of ‘int *’ [-Wsizeof-array-argument]
w03.c:5:25: warning: argument to ‘sizeof’ in ‘memset’ call is the same expression as the destination; did you mean to dereference it? [-Wsizeof-pointer-memaccess]
```

Fix: pass the element count, or take a pointer to the array type (`int (*a)[8]`).

**W04: comparing strings with `==`.** It compares addresses, and the result depends on whether the compiler merged two literals:

```text
w04.c:6:11: warning: comparison with string literal results in unspecified behavior [-Waddress]
```

Fix: `strcmp(s, "yes") == 0`.

**W05: `char c` against `EOF`.** `EOF` is the `int` -1. The byte 0xFF stored in a signed `char` is also -1, so the loop stops there:

```text
2 characters read
```

Four of the six bytes were never counted (the program counted 2). On a platform where `char` is unsigned the comparison is never true and the loop never ends (from memory, not measured). Fix: `int c`.

**W06: `memcpy` on overlapping regions.** Undefined. On this glibc the result happens to equal the `memmove` result:

```text
ababcdef
ababcdef
```

That is the danger: the test passes, and the bug appears with another size, another library or another CPU path. Use `memmove` whenever the regions may overlap.

**W07: an unsigned loop counter with `>= 0`.** Always true; after index 0 the counter wraps to `SIZE_MAX` and the loop reads far out of bounds:

```text
w07.c:6:30: warning: comparison of unsigned expression in ‘>= 0’ is always true [-Wtype-limits]
```

Fix: `for (size_t i = n; i-- > 0;)`, or iterate forward.

**W08: an unbraced multi-statement macro.** Only the first statement of `SWAP` is controlled by the `if`:

```text
2 99
```

`x = y` and `y = tmp` ran although `n > 0` is false: x is 2 and y is the stale 99. Fix: wrap in `do { ... } while (0)` with a block-local temporary, or use a `static inline` function (C 06).

**W09: four mistakes in five lines.**

```text
w09.c:6:13: warning: format ‘%d’ expects argument of type ‘int *’, but argument 2 has type ‘int’ [-Wformat=]
w09.c:9:31: warning: division ‘sizeof (int *) / sizeof (int)’ does not compute the number of array elements [-Wsizeof-pointer-div]
w09.c:6:5: warning: ignoring return value of ‘scanf’ declared with attribute ‘warn_unused_result’ [-Wunused-result]
w09.c:6:5: warning: ‘n’ is used uninitialized [-Wuninitialized]
```

`scanf("%d", n)` needs `&n`; the result of `scanf` is ignored; `n` is therefore uninitialised; and `sizeof(p) / sizeof(p[0])` on a pointer is 2, not 4. Compile with `-Wall -Wextra` and read every warning.

**W10: `counter++` from two threads.** Not atomic: it is a load, an add and a store, and the threads overwrite each other:

```text
lost updates
```

All five runs I made printed this (I ran it five times in a separate command). Fix: `atomic_fetch_add`, a mutex, or per-thread counters summed at the end; ThreadSanitizer finds the race (C 10).

**W11: `memcmp` on a struct with padding.** The fields are equal, but the 3 bytes between `c` and `i` and the tail were set differently:

```text
fields equal: 1, memcmp equal: 0
```

Compare field by field (or zero the whole struct before filling it, which is fragile when someone copies a struct by assignment: the compiler may leave the padding of the copy as it likes).

**W12: use after free in a free loop.** `head->next` is read after `free(head)`:

```text
ERROR: AddressSanitizer: heap-use-after-free on address ADDR at pc ADDR bp ADDR sp ADDR
READ of size 8 at ADDR thread T0
    #0 main w12.c:17
    #2 __libc_start_main_impl ../csu/libc-start.c:360
ADDR is located 8 bytes inside of 16-byte region [ADDR,ADDR)
    #1 main w12.c:16
    #1 main w12.c:10
SUMMARY: AddressSanitizer: heap-use-after-free w12.c:17 in main
```

Fix: save the next pointer first (`Node *next = head->next; free(head); head = next;`), which is the shape of `list_free` in the next section.

---

## 5. Implement it: a ring buffer, list operations and a fixed-block allocator

Whiteboard questions test whether you can write a small data structure with no off-by-one, say what its invariants are, and test it. `memcpy`, `memmove`, `atoi` and `strtok` are already written, with tests, in C 07; an SPSC ring buffer with a measured memory-order argument is in C 10. This section adds the versions that interviewers ask for in this order.

### 5.1 The ring buffer

**What the interviewer listens for:** (1) how you tell full from empty, (2) what happens when the indices wrap, (3) the cost of the wrap-around (a modulo is a division), (4) whether you can do a bulk copy in at most two `memcpy` calls, (5) what changes for two threads.

The design below answers all five. The capacity is a power of two so that the index is `counter & mask` (one AND). The counters are **free running**: they are never reduced, so `head - tail` is the fill level, 0 means empty and `capacity` means full, and no slot is wasted. Unsigned subtraction stays correct after the counters wrap around 2^32, as long as the capacity divides 2^32 (which is why it must be a power of two).

```c
/* A byte ring buffer. The capacity is a power of two, head and tail are free running counters
   (they are never reduced), the index is counter & mask, and the fill level is head - tail.
   Unsigned subtraction is correct even after the counters wrap around 2^32. */
typedef struct {
    uint8_t *buf;
    uint32_t mask;          /* capacity - 1 */
    uint32_t head;          /* total bytes ever written */
    uint32_t tail;          /* total bytes ever read */
} Ring;

static int ring_init(Ring *r, uint8_t *buf, uint32_t capacity)
{
    if (capacity == 0 || (capacity & (capacity - 1)) != 0)
        return -1;
    r->buf = buf;
    r->mask = capacity - 1;
    r->head = r->tail = 0;
    return 0;
}

static uint32_t ring_count(const Ring *r) { return r->head - r->tail; }
static uint32_t ring_space(const Ring *r) { return r->mask + 1 - ring_count(r); }

static int ring_put(Ring *r, uint8_t b)
{
    if (ring_space(r) == 0)
        return -1;
    r->buf[r->head++ & r->mask] = b;
    return 0;
}

static int ring_get(Ring *r, uint8_t *b)
{
    if (ring_count(r) == 0)
        return -1;
    *b = r->buf[r->tail++ & r->mask];
    return 0;
}

/* Bulk write: at most two memcpy calls (up to the end of the array, then from the start). */
static uint32_t ring_write(Ring *r, const uint8_t *src, uint32_t n)
{
    uint32_t space = ring_space(r);
    if (n > space)
        n = space;
    uint32_t pos = r->head & r->mask;
    uint32_t first = r->mask + 1 - pos;
    if (first > n)
        first = n;
    memcpy(r->buf + pos, src, first);
    memcpy(r->buf, src + first, n - first);
    r->head += n;
    return n;
}

static uint32_t ring_read(Ring *r, uint8_t *dst, uint32_t n)
{
    uint32_t avail = ring_count(r);
    if (n > avail)
        n = avail;
    uint32_t pos = r->tail & r->mask;
    uint32_t first = r->mask + 1 - pos;
    if (first > n)
        first = n;
    memcpy(dst, r->buf + pos, first);
    memcpy(dst + first, r->buf, n - first);
    r->tail += n;
    return n;
}
```

The common first attempt keeps `head` and `tail` reduced modulo the capacity and says "full when `(head + 1) % cap == tail`". That wastes one slot, and when the caller forgets to check, `head == tail` means empty again after a full lap. Problem B shows it.

**The lock-free version.** With **one** producer thread (or interrupt handler) and **one** consumer, no lock is needed if each side writes only its own counter and the data is published with a release store:

```c
/* The same ring for ONE producer thread (or interrupt handler) and ONE consumer thread.
   Each side writes only its own counter. The producer publishes the data with a release store
   of head, the consumer reads head with acquire; the consumer frees a slot with a release store
   of tail, the producer reads tail with acquire. No lock, no read-modify-write. */
typedef struct {
    uint8_t *buf;
    uint32_t mask;
    _Atomic uint32_t head;
    _Atomic uint32_t tail;
} Spsc;

static int spsc_init(Spsc *r, uint8_t *buf, uint32_t capacity)
{
    if (capacity == 0 || (capacity & (capacity - 1)) != 0)
        return -1;
    r->buf = buf;
    r->mask = capacity - 1;
    atomic_init(&r->head, 0);
    atomic_init(&r->tail, 0);
    return 0;
}

static int spsc_put(Spsc *r, uint8_t b)               /* producer only */
{
    uint32_t head = atomic_load_explicit(&r->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (head - tail == r->mask + 1)
        return -1;
    r->buf[head & r->mask] = b;
    atomic_store_explicit(&r->head, head + 1, memory_order_release);
    return 0;
}

static int spsc_get(Spsc *r, uint8_t *b)              /* consumer only */
{
    uint32_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&r->head, memory_order_acquire);
    if (head == tail)
        return -1;
    *b = r->buf[tail & r->mask];
    atomic_store_explicit(&r->tail, tail + 1, memory_order_release);
    return 0;
}
```

The producer writes the slot, then publishes it with a release store of `head`; the consumer reads `head` with acquire, so it sees the slot's contents. The consumer's release store of `tail` hands the slot back. The producer's own counter needs only a relaxed load. This is wrong for two producers (they would both read the same `head`).

I ran it with a producer thread and a consumer thread passing 20,000,000 bytes, each byte a function of its index, and with ThreadSanitizer on a shorter run:

```text
T2 20000000 bytes through the ring: 20000000 bytes through the ring, 0 wrong; ThreadSanitizer reports: correct ring 0, relaxed-store ring 1 race reported; relaxed-store ring at -O2 on x86: 20000000 bytes through the ring, 0 wrong
```

| Run | Time for 20,000,000 bytes, ms (run 1 / run 2) |
|---|---|
| two threads, one byte per call | 409 / 475 |

All bytes arrived in order, and ThreadSanitizer reported nothing for the correct ring. The same ring with `memory_order_relaxed` instead of `release` in `spsc_put` **was reported as a data race by ThreadSanitizer**, but it produced no wrong byte on this x86-64 machine at `-O2` in 20,000,000 bytes: x86 does not reorder a store with an earlier store, so the bug is invisible there and would appear on ARM or POWER (from memory, not measured here). That is why the single-threaded test suite below also lets this mutant escape. A memory-order bug is found by a tool that models the language, not by a test on a strong machine.

**Tests.** The test program compares every operation with a model queue (a plain array), on random sequences of `put`, `get`, bulk `write` and bulk `read`, with capacities 1 to 64 and, half the time, counters that start 16 below 2^32 so that they wrap during the run. It makes 11,226,979 checks (default), and passes under ASan and UBSan.

### 5.2 Linked list operations

**What the interviewer listens for:** the three-pointer reverse, Floyd's cycle test (and why it terminates), the middle in one pass, the two-pointer "n-th from the end", and the pointer-to-pointer idiom that removes the head special case. Merge must be stable (the node of the first list first on equal values).

```c
typedef struct Node {
    int v;
    int tag;                /* identifies a node when values are equal (to test stability) */
    struct Node *next;
} Node;

static Node *list_push(Node *head, int v, int tag)
{
    Node *n = malloc(sizeof *n);
    if (!n)
        abort();
    n->v = v;
    n->tag = tag;
    n->next = head;
    return n;
}

static void list_free(Node *h)
{
    while (h) {
        Node *next = h->next;
        free(h);
        h = next;
    }
}

/* Reverse in place: three pointers, one pass. */
static Node *list_reverse(Node *h)
{
    Node *prev = NULL;
    while (h) {
        Node *next = h->next;
        h->next = prev;
        prev = h;
        h = next;
    }
    return prev;
}

/* Floyd's tortoise and hare: O(n) time, O(1) space. */
static int list_has_cycle(const Node *h)
{
    const Node *slow = h, *fast = h;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
        if (slow == fast)
            return 1;
    }
    return 0;
}

/* The middle node; for an even length, the second of the two middle nodes. NULL for an empty list. */
static Node *list_middle(Node *h)
{
    Node *slow = h, *fast = h;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
    }
    return slow;
}

/* Remove and free the n-th node from the end (n = 1 is the last node). Returns the new head.
   A list shorter than n is returned unchanged. Two pointers, n apart, one pass. */
static Node *list_remove_nth_from_end(Node *head, int n)
{
    if (n < 1)
        return head;
    Node *lead = head;
    for (int i = 0; i < n; ++i) {
        if (!lead)
            return head;
        lead = lead->next;
    }
    Node **link = &head;                      /* the pointer that points at the node to delete */
    while (lead) {
        lead = lead->next;
        link = &(*link)->next;
    }
    Node *dead = *link;
    *link = dead->next;
    free(dead);
    return head;
}

/* Merge two sorted lists into one, stable: on equal values the node of a comes first. */
static Node *list_merge(Node *a, Node *b)
{
    Node dummy, *tail = &dummy;
    while (a && b) {
        if (b->v < a->v) {
            tail->next = b;
            b = b->next;
        } else {
            tail->next = a;
            a = a->next;
        }
        tail = tail->next;
    }
    tail->next = a ? a : b;
    return dummy.next;
}
```

Notes. `list_remove_nth_from_end` moves a lead pointer `n` nodes ahead and then advances both until the lead falls off the end; `link` is the address of the pointer that points at the node to delete, so deleting the head and deleting an interior node are the same three lines. `list_merge` uses a dummy node on the stack so that the first append needs no special case; the `<` (and not `<=`) is what makes it stable. Floyd's algorithm: if there is a cycle, the fast pointer gains one node per step on the slow one inside the cycle, so they meet within the cycle length.

**Tests.** Random lists of 0 to 12 nodes with values from 0 to 5 (so there are many equal values) are checked against arrays: reverse, middle (the second of two for an even length), a cycle from the last node to a random node, `remove_nth_from_end` for every `n` from 0 to length plus 2, and a merge of two sorted lists in which each node carries a tag that identifies its list and position, so that the stability check can tell equal values apart.

### 5.3 A fixed-block allocator

C 03 built a pool allocator as part of the allocator tour. The version here is the one for an interview about firmware or a network stack: blocks carved from a caller-supplied buffer, no per-block header, O(1) allocate and free, alignment handled, and `pool_free` that **rejects** a pointer that is foreign, not at the start of a block, or already free (a double free in an embedded system silently corrupts the free list):

```c
/* A fixed-block allocator over a caller-supplied buffer. Free blocks form a list threaded through
   the blocks themselves (no header, no extra memory). A caller-supplied array `used` of one byte
   per block lets pool_free reject a pointer that is foreign, misaligned or already free. */
typedef struct {
    uint8_t *base;          /* first block, aligned */
    size_t block;           /* block size, a multiple of the alignment */
    size_t count;           /* number of blocks */
    size_t nfree;
    void *free_head;
    uint8_t *used;
} Pool;

#define POOL_ALIGN 16

/* Returns the number of blocks (0 if the buffer is too small). */
static size_t pool_init(Pool *p, void *mem, size_t mem_size, size_t block, uint8_t *used)
{
    uintptr_t start = ((uintptr_t)mem + POOL_ALIGN - 1) & ~(uintptr_t)(POOL_ALIGN - 1);
    size_t lost = start - (uintptr_t)mem;
    if (block < sizeof(void *))
        block = sizeof(void *);
    block = (block + POOL_ALIGN - 1) & ~(size_t)(POOL_ALIGN - 1);
    p->count = mem_size > lost ? (mem_size - lost) / block : 0;
    p->base = (uint8_t *)start;
    p->block = block;
    p->nfree = p->count;
    p->used = used;
    p->free_head = NULL;
    for (size_t i = p->count; i-- > 0;) {          /* build the list so that block 0 is allocated first */
        void **b = (void **)(p->base + i * block);
        *b = p->free_head;
        p->free_head = b;
        used[i] = 0;
    }
    return p->count;
}

static void *pool_alloc(Pool *p)
{
    void **b = p->free_head;
    if (!b)
        return NULL;
    p->free_head = *b;
    p->used[((uint8_t *)b - p->base) / p->block] = 1;
    p->nfree--;
    return b;
}

/* 0 on success; -1 for a pointer that is not the start of a block of this pool; -2 for a double free. */
static int pool_free(Pool *p, void *ptr)
{
    uint8_t *b = ptr;
    if (b < p->base || b >= p->base + p->count * p->block || (size_t)(b - p->base) % p->block != 0)
        return -1;
    size_t i = (size_t)(b - p->base) / p->block;
    if (!p->used[i])
        return -2;
    p->used[i] = 0;
    *(void **)b = p->free_head;
    p->free_head = b;
    p->nfree++;
    return 0;
}
```

Design points to say aloud. The free list lives inside the free blocks (a free block is at least a pointer wide), so the allocator needs no memory of its own except the `used` array, which is the price of detecting double frees (one byte per block here; a bit per block with a bitmap is the usual refinement). The block size is rounded up to a multiple of 16, and the base pointer is aligned, so every returned pointer is 16 byte aligned. There is **no fragmentation** by construction: every block is the same size. It is **not thread safe**; to share it with an interrupt handler either disable interrupts around the two list operations or give the interrupt its own pool.

**Tests.** The test program does random allocations and frees against a model (a set of live blocks), with a random buffer offset (so the alignment correction is used), random block sizes of 1 to 100 bytes and random buffer sizes. It checks the number of blocks, that every returned pointer is aligned, inside the buffer and distinct from the others, that a block's contents are intact when it is freed (each block is filled with a pattern that depends on its position), that `NULL` is returned exactly when the pool is exhausted, and that the three kinds of bad `pool_free` are rejected: a pointer to a local variable, a pointer one byte inside a block, and the pointer one past the last block, and that a second free of the same block returns -2.

### 5.4 Do the tests notice a bug?

As in C 11, a passing test run means little until it fails when it should. I made 30 deliberate defects in the three data structures: the index reduced with `%` instead of `&`, the full test one slot early, the counters masked before the subtraction, a split of the bulk write one byte early, no clamp of the write size, the cycle test that only works through the head, a cycle test that moves the fast pointer one step, a reverse that sets every `next` to NULL, a removal that frees nothing, an unstable merge, a pool that does not round the block size, one that does not align the base, one that does not detect a double free, and so on. Each was compiled in and the tests run:

```text
caught ring_space is one too small check
caught ring_put reports full one byte early check
caught ring_put reduces the index with modulo mask instead of and check
caught ring_get tests empty wrongly for large counters check
caught ring_count reduces the counters before subtracting check
caught ring_write splits one byte before the end check
caught ring_read does not advance the tail check
caught ring_write does not clamp to the space check
caught ring_init accepts a size that is not a power of two check
caught spsc_put reports full one byte early check
caught spsc_get does not publish the tail check
ESCAPED spsc_put publishes head with a relaxed store
caught list_reverse sets every next to NULL check
caught list_has_cycle moves the fast pointer one step check
caught list_has_cycle only finds a cycle through the head timeout
caught list_middle returns the first middle and crashes on an empty list check
caught list_remove_nth_from_end removes the wrong node check
caught list_remove_nth_from_end does not reject n < 1 sanitizer/crash
caught list_remove_nth_from_end leaks the node sanitizer/crash
caught list_merge is not stable check
caught list_merge drops the rest of b check
caught pool_free does not detect a double free check
caught pool_free accepts a pointer inside a block sanitizer/crash
caught pool_free accepts the pointer one past the last block check
caught pool_init does not round the block size sanitizer/crash
caught pool_init ignores the bytes lost to alignment check
caught pool_alloc does not mark the block used check
caught pool_free does not return the block to the list check
caught pool_alloc does not count the block check
caught pool_init does not align the base sanitizer/crash
29 of 30
```

**29 were caught**: 23 by a failing check, 5 by a sanitizer or a crash (including the node that is never freed, found by LeakSanitizer) and 1 by a timeout (with that defect the cycle test never returns when the cycle does not pass through the head). **One escaped**: `spsc_put` with a relaxed store of `head`. A single-threaded test cannot see a memory-order bug; the section above shows what finds it.

---

## 6. Embedded C

Firmware and driver interviews add one theme to everything above: the program shares data with something that runs by itself (a peripheral, an interrupt handler, a watchdog), so the compiler's assumption that "nobody else changes my variables" is false. The questions below are the ones that come up in this order.

### 6.1 `volatile`: the program that hangs

**What does `volatile` do, and what does it not do?** It tells the compiler that every access to the object is observable: each read and write in the source must happen, in the source order, relative to other volatile accesses. It is for memory-mapped registers, for flags changed by a signal or interrupt handler, and for variables that must survive `longjmp`. It does **not** make an access atomic, and it does not order anything between threads; cppreference's page on `volatile` says so (checked: volatile objects are "not suitable for communication between threads", an unsynchronised access is a data race). Here is the program, with a signal handler standing in for an interrupt handler:

```c
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#ifdef FLAG_VOLATILE
static volatile sig_atomic_t done;
#else
static int done;
#endif

static void on_alarm(int sig)
{
    (void)sig;
    done = 1;                       /* the "interrupt handler" */
}

int main(void)
{
    signal(SIGALRM, on_alarm);
    alarm(1);
    unsigned long spins = 0;
    while (!done)
        spins++;                    /* the main loop waits for the handler */
    printf("flag seen\n");
    return 0;
}
```

I built it three ways and ran each under `timeout`:

```text
-O0, plain int flag:
flag seen
exit status 0
-O2, plain int flag (timeout 3 s):
exit status 124
-O2, volatile sig_atomic_t flag:
flag seen
exit status 0
```

At `-O0` the loop re-reads `done` from memory on every pass and exits when the handler sets it. At `-O2` the compiler sees that nothing in the loop changes `done`, loads it once and spins forever: the process was killed by `timeout` after 3 seconds (exit status 124). With `volatile sig_atomic_t` it exits at once. `sig_atomic_t` is the type the standard guarantees to be read and written in one piece with respect to a signal handler; for an interrupt on a microcontroller the equivalent is a variable no wider than the machine's natural word (an 8 bit AVR cannot read a 16 bit counter in one instruction, so a 16 bit counter shared with an interrupt can be read torn; from memory, not measured).

**Related questions.** `const volatile uint32_t *p` is a read-only hardware status register: the program must not write it, and the compiler must not cache it. `volatile uint32_t * const p` is a constant address of a volatile register (the usual way to name a register). `volatile` on a struct member of a register block (as `UartRegs` below) makes every member access a real access.

### 6.2 A driver against a device model

A register is a volatile object at a fixed address, and reading or writing it has an effect: reading a data register pops a byte, a status flag may be cleared by writing a 1 to it. To test a driver on a host, the accessors are replaced by calls into a model of the device. The driver code is the same:

```c
/* Register access. On the target a register is a volatile object at a fixed address and every
   access is a real load or store. In a host test build the same driver code calls functions
   that model the device (compile with -DSIM). */
#ifdef SIM
uint32_t sim_read(volatile uint32_t *reg);
void sim_write(volatile uint32_t *reg, uint32_t v);
#define REG_RD(r) sim_read(&(r))
#define REG_WR(r, v) sim_write(&(r), (v))
#else
#define REG_RD(r) (r)
#define REG_WR(r, v) ((r) = (v))
#endif

typedef struct {
    volatile uint32_t SR;       /* status */
    volatile uint32_t DR;       /* data: a write transmits, a read receives */
    volatile uint32_t CR;       /* control */
} UartRegs;

#define UART_SR_TXE  (1u << 0)  /* transmitter ready for a byte (read only) */
#define UART_SR_RXNE (1u << 1)  /* a received byte is waiting in DR (cleared by reading DR) */
#define UART_SR_OVR  (1u << 2)  /* receive overrun: write 1 to clear */
#define UART_SR_FE   (1u << 3)  /* framing error: write 1 to clear */
#define UART_CR_RXIE (1u << 0)  /* receive interrupt enable */

/* A field of a register: mask selects the bits, shift is the position of the lowest bit. */
#define FIELD_GET(v, mask, shift) (((v) & (mask)) >> (shift))
#define FIELD_SET(v, mask, shift, x) (((v) & ~(uint32_t)(mask)) | (((uint32_t)(x) << (shift)) & (mask)))
```

On the target `REG_RD` and `REG_WR` are plain volatile accesses; with `-DSIM` they call `sim_read` and `sim_write`, which the test program defines, so that the model can react to each access. `FIELD_SET` and `FIELD_GET` are the macros for a multi-bit field (read-modify-write the field, leave the other bits alone, mask the value so that a too-wide value does not spill into the neighbour).

The driver: a polled transmit with a timeout, an interrupt handler that fills a receive ring, and a `getc` for the main loop. The receive ring is the lock-free single-producer, single-consumer ring of section 5.1: the interrupt handler is the producer and the main loop the consumer, so no interrupt masking is needed.

```c
typedef struct {
    UartRegs *regs;
    Spsc rx;                    /* the interrupt handler produces, the main loop consumes */
    uint8_t rxbuf[8];
    uint32_t overruns, framing, dropped;
} Uart;

static void uart_init(Uart *u, UartRegs *regs)
{
    u->regs = regs;
    spsc_init(&u->rx, u->rxbuf, sizeof u->rxbuf);
    u->overruns = u->framing = u->dropped = 0;
    REG_WR(regs->CR, UART_CR_RXIE);
}

/* Polled transmit with a timeout: never spin forever on a device that may not answer. */
static int uart_putc(Uart *u, char c, unsigned spins)
{
    while (!(REG_RD(u->regs->SR) & UART_SR_TXE))
        if (spins-- == 0)
            return -1;
    REG_WR(u->regs->DR, (uint32_t)(unsigned char)c);
    return 0;
}

/* The interrupt handler: short, no allocation, no printing, no blocking. */
static void uart_isr(Uart *u)
{
    uint32_t sr = REG_RD(u->regs->SR);
    if (sr & UART_SR_OVR) {
        u->overruns++;
        REG_WR(u->regs->SR, UART_SR_OVR);          /* write 1 to THAT bit only */
    }
    if (sr & UART_SR_FE) {
        u->framing++;
        REG_WR(u->regs->SR, UART_SR_FE);
    }
    if (sr & UART_SR_RXNE) {
        uint8_t b = (uint8_t)REG_RD(u->regs->DR);  /* reading DR clears RXNE */
        if (spsc_put(&u->rx, b) != 0)
            u->dropped++;
    }
}

static int uart_getc(Uart *u)                      /* main loop; -1 when nothing is waiting */
{
    uint8_t b;
    return spsc_get(&u->rx, &b) == 0 ? (int)b : -1;
}
```

The rules the handler follows, which are also the answers to "what must an ISR not do": it is short; it does no allocation, no `printf`, no blocking call and no locking (a lock held by the interrupted code would deadlock it); it touches shared state only through a volatile or atomic object; it acknowledges the device so that the interrupt does not fire again at once; and it clears a flag by writing only that flag.

The test program models the transmitter (busy for 3 status reads after each byte), the receive FIFO, a stuck device, and two status flags that clear when a 1 is written to them. The output, line by line:

```c
#define SIM
#include <stdio.h>
#include "../../code/emb.h"

/* ---- a model of the peripheral: every register access is a call, so the model can react ---- */
static UartRegs regs;
static struct {
    unsigned busy;                  /* SR reads left until the transmitter is ready again */
    int stuck;                      /* the transmitter never becomes ready */
    char tx[32];
    int ntx;
    uint8_t rxfifo[32];
    int nrx, rxpos;
    uint32_t flags;                 /* OVR and FE, set by the test */
    unsigned sr_reads, lost_tx;
    unsigned arm_fe;                /* set FE after this many status reads: an error that arrives mid-handler */
} dev;

uint32_t sim_read(volatile uint32_t *reg)
{
    if (reg == &regs.SR) {
        dev.sr_reads++;
        uint32_t v = dev.flags;
        if (dev.busy == 0 && !dev.stuck)
            v |= UART_SR_TXE;
        else if (dev.busy > 0)
            dev.busy--;
        if (dev.rxpos < dev.nrx)
            v |= UART_SR_RXNE;
        if (dev.arm_fe && dev.sr_reads == dev.arm_fe)
            dev.flags |= UART_SR_FE;
        return v;
    }
    if (reg == &regs.DR)
        return dev.rxpos < dev.nrx ? dev.rxfifo[dev.rxpos++] : 0;
    return *reg;
}

void sim_write(volatile uint32_t *reg, uint32_t v)
{
    if (reg == &regs.DR) {
        if (dev.busy > 0 || dev.stuck)
            dev.lost_tx++;
        else {
            dev.tx[dev.ntx++] = (char)v;
            dev.busy = 3;
        }
    } else if (reg == &regs.SR) {
        dev.flags &= ~(v & (UART_SR_OVR | UART_SR_FE));     /* write 1 to clear */
    } else {
        *reg = v;
    }
}

/* The bug: clear a flag with a read-modify-write. It writes 1 to every bit that is set. */
static void isr_bad(Uart *u)
{
    uint32_t sr = REG_RD(u->regs->SR);
    if (sr & UART_SR_OVR) {
        u->overruns++;
        REG_WR(u->regs->SR, REG_RD(u->regs->SR) | UART_SR_OVR);
    }
    if (sr & UART_SR_FE)
        u->framing++;
    if (sr & UART_SR_RXNE)
        (void)REG_RD(u->regs->DR);
}

static void reset_dev(void)
{
    dev.busy = 0;
    dev.stuck = 0;
    dev.ntx = 0;
    dev.nrx = dev.rxpos = 0;
    dev.flags = 0;
    dev.sr_reads = dev.lost_tx = 0;
    dev.arm_fe = 0;
}

int main(void)
{
    Uart u;
    uart_init(&u, &regs);

    /* 1. polled transmit: the device is busy for 3 status reads after each byte */
    const char *msg = "hello";
    int failed = 0;
    for (const char *p = msg; *p; ++p)
        failed += uart_putc(&u, *p, 100) != 0;
    dev.tx[dev.ntx] = '\0';
    printf("1. sent \"%s\", %d failed, %u status reads, %u bytes lost\n", dev.tx, failed, dev.sr_reads, dev.lost_tx);

    /* 2. the same loop without the wait: the device drops bytes it is not ready for */
    reset_dev();
    for (const char *p = msg; *p; ++p)
        REG_WR(regs.DR, (uint32_t)(unsigned char)*p);
    dev.tx[dev.ntx] = '\0';
    printf("2. no wait: the device kept \"%s\", lost %u bytes\n", dev.tx, dev.lost_tx);

    /* 3. a transmitter that never answers: the timeout turns a hang into an error */
    reset_dev();
    dev.stuck = 1;
    int rc = uart_putc(&u, 'x', 50);
    printf("3. stuck device: uart_putc returned %d after %u status reads\n", rc, dev.sr_reads);

    /* 4. receive: 12 bytes arrive; the interrupt handler runs after each; the main loop reads 1 per 3 */
    reset_dev();
    for (int i = 0; i < 12; ++i)
        dev.rxfifo[i] = (uint8_t)('a' + i);
    dev.nrx = 12;
    char got[16];
    int ng = 0;
    for (int i = 0; i < 12; ++i) {
        uart_isr(&u);
        if (i % 3 == 2) {
            int c = uart_getc(&u);
            if (c >= 0)
                got[ng++] = (char)c;
        }
    }
    int c;
    while ((c = uart_getc(&u)) >= 0)
        got[ng++] = (char)c;
    got[ng] = '\0';
    printf("4. received 12, got \"%s\" (%d), dropped %u: the 8 byte ring was full\n", got, ng, u.dropped);

    /* 5. write-1-to-clear: an overrun is pending; a framing error arrives while the handler runs */
    Uart a, b;
    uart_init(&a, &regs);
    uart_init(&b, &regs);
    reset_dev();
    dev.flags = UART_SR_OVR;
    dev.arm_fe = 1;
    uart_isr(&a);
    uart_isr(&a);                       /* the second interrupt, raised by the framing error */
    reset_dev();
    dev.flags = UART_SR_OVR;
    dev.arm_fe = 1;
    isr_bad(&b);
    uart_isr(&b);
    printf("5. write 1 to the bit only: %u overrun, %u framing error. read-modify-write: %u overrun, %u framing error\n",
           a.overruns, a.framing, b.overruns, b.framing);

    /* 6. register fields */
    uint32_t cr = 0xF0F0u;
    cr = FIELD_SET(cr, 0x0F00u, 8, 5);
    printf("6. FIELD_SET -> %#x, FIELD_GET -> %u, a value too wide for the field is masked: %#x\n",
           cr, FIELD_GET(cr, 0x0F00u, 8), FIELD_SET(0u, 0x0F00u, 8, 0x1Fu));
    return 0;
}
```

```text
1. sent "hello", 0 failed, 17 status reads, 0 bytes lost
2. no wait: the device kept "h", lost 4 bytes
3. stuck device: uart_putc returned -1 after 51 status reads
4. received 12, got "abcdefghijk" (11), dropped 1: the 8 byte ring was full
5. write 1 to the bit only: 1 overrun, 1 framing error. read-modify-write: 1 overrun, 0 framing error
6. FIELD_SET -> 0xf5f0, FIELD_GET -> 5, a value too wide for the field is masked: 0xf00
```

1. **Polled transmit.** 5 bytes, none lost: 17 status reads, because after each byte the driver spins 3 reads for a busy device and 1 for the ready read.
2. **No wait.** Writing the data register without checking the status: the device kept the first byte and lost 4. The classic "works with a slow loop, loses data when the code is optimised".
3. **A device that never answers.** The timeout turns an infinite loop into an error after 51 status reads. Never write `while (!(SR & TXE));` without a bound in code that must recover.
4. **Receive.** 12 bytes arrive; the handler runs after each; the main loop reads only one byte for every three. The 8 byte ring fills and the last byte is dropped, and the driver counts it: `dropped` is the observable that tells you the consumer is too slow.
5. **Write-1-to-clear.** An overrun is pending and a framing error arrives while the handler runs. The correct handler writes only the overrun bit and the framing error is seen by the next interrupt. The handler that clears the flag by read-modify-write (`SR |= OVR`) writes a 1 to **every** bit that is set at that moment, including the framing error it never looked at: that error is lost (0 counted). This bug is invisible in testing with a real device, where errors are rare.
6. **Register fields.** `FIELD_SET` replaced bits 11 to 8 and left the rest; `FIELD_GET` read them back; a value too wide for the field is masked.

### 6.3 The watchdog

A watchdog is a hardware counter that counts up by itself and resets the system when it reaches a limit unless software "kicks" it in time. The interview question is not how to write the kick; it is **where** to put it. A kick from a timer interrupt proves only that interrupts still run. The kick must come from the main loop, and only when every task that has to make progress has checked in since the last kick:

```c
/* A watchdog model: the hardware counter counts up by itself and resets the system when it
   reaches the timeout. Software must "kick" it. The rule: kick only from the main loop, and only
   when every task that must make progress has checked in since the last kick. */
typedef struct {
    uint32_t timeout, counter;
    uint32_t required, seen;    /* bit masks of tasks */
    uint32_t resets;
} Wdt;

static void wdt_init(Wdt *w, uint32_t timeout, uint32_t required)
{
    w->timeout = timeout;
    w->counter = 0;
    w->required = required;
    w->seen = 0;
    w->resets = 0;
}

static void wdt_checkin(Wdt *w, unsigned task) { w->seen |= 1u << task; }

static int wdt_service(Wdt *w)
{
    if ((w->seen & w->required) != w->required)
        return 0;
    w->seen = 0;
    w->counter = 0;                                 /* the kick */
    return 1;
}

static void wdt_tick(Wdt *w)                        /* the hardware, once per time unit */
{
    if (++w->counter >= w->timeout) {
        w->resets++;
        w->counter = 0;
        w->seen = 0;
    }
}
```

The model counts in ticks; three tasks must check in; the timeout is 50 ticks. Scenario A kicks from the main loop after all three tasks have checked in, and task 2 hangs from tick 500. Scenario B kicks from a "timer interrupt" regardless of the tasks. Scenario C is a healthy system.

```c
#include <stdio.h>
#include "../../code/emb.h"

#define TASKS 3u
#define TIMEOUT 50u

int main(void)
{
    /* A: kick from the main loop, only when all three tasks checked in. Task 2 hangs at tick 500. */
    Wdt w;
    wdt_init(&w, TIMEOUT, (1u << TASKS) - 1);
    unsigned first_reset = 0;
    for (unsigned tick = 1; tick <= 1000; ++tick) {
        for (unsigned t = 0; t < TASKS; ++t)
            if (!(t == 2 && tick >= 500))
                wdt_checkin(&w, t);
        wdt_service(&w);
        uint32_t before = w.resets;
        wdt_tick(&w);
        if (w.resets > before && !first_reset)
            first_reset = tick;
    }
    printf("A. kick from the main loop after all tasks checked in: first reset at tick %u, %u resets in 1000 ticks\n", first_reset, w.resets);

    /* B: the same system, but the kick is done from a timer interrupt that does not look at the tasks. */
    wdt_init(&w, TIMEOUT, (1u << TASKS) - 1);
    for (unsigned tick = 1; tick <= 1000; ++tick) {
        w.counter = 0;                    /* the kick, whatever the tasks are doing */
        wdt_tick(&w);
    }
    printf("B. kick from a timer interrupt, task 2 hung since tick 500: %u resets, the hang is never detected\n", w.resets);

    /* C: healthy system */
    wdt_init(&w, TIMEOUT, (1u << TASKS) - 1);
    for (unsigned tick = 1; tick <= 1000; ++tick) {
        for (unsigned t = 0; t < TASKS; ++t)
            wdt_checkin(&w, t);
        wdt_service(&w);
        wdt_tick(&w);
    }
    printf("C. all tasks healthy: %u resets in 1000 ticks\n", w.resets);
    return 0;
}
```

```text
A. kick from the main loop after all tasks checked in: first reset at tick 548, 10 resets in 1000 ticks
B. kick from a timer interrupt, task 2 hung since tick 500: 0 resets, the hang is never detected
C. all tasks healthy: 0 resets in 1000 ticks
```

In A the last good kick was at tick 499, so the watchdog fired at tick 548 (the counter counted 50 from tick 499) and keeps firing every 50 ticks, which is what a reset loop looks like. In B the hung task is never detected: 0 resets. In C there are none. Other points interviewers expect: the watchdog is the last line of defence, so after a reset the code must record the reason (a reset-cause register) and not hide it; do not kick it inside a long delay or from the interrupt handler; during a firmware update the timeout may need to be longer; and test it by hanging a task on purpose, as above.

### 6.4 More embedded questions, short answers

*These are from memory and not run, except where an earlier section measured the point.*

- **Why is `x++` on a `volatile` not atomic?** It is a load, an add and a store; an interrupt can occur between them. Use the architecture's atomic instruction, `<stdatomic.h>`, or disable interrupts around it.
- **Struct overlay on a register block.** `UartRegs` works if the compiler's padding matches the hardware. Check with `_Static_assert(offsetof(UartRegs, DR) == 4, ...)` (C 04, C 10); packed structs avoid padding but can generate unaligned accesses, which fault on some ARM cores.
- **Interrupt latency and stack.** An interrupt handler runs on the current stack (or a dedicated one): keep local variables small, and count the worst-case nesting.
- **Memory barriers.** A compiler barrier is `__asm__ volatile("" ::: "memory")`; a hardware barrier (`dmb`, `dsb` on ARM) orders accesses as seen by other bus masters. `volatile` gives neither. Used when a descriptor in memory must be visible before a register write starts a DMA.
- **Endianness and bit order.** Hardware register bit positions are fixed by the datasheet; bit-fields have implementation-defined order, so use shifts and masks (C 07).
- **Why avoid `malloc` in firmware?** Fragmentation, unpredictable time, failure with no recovery: use fixed-block pools (section 5.3) and static allocation. This is also the MISRA position (C 11).

---

## 7. What it costs

Duff's device against a plain loop and `memcpy` for a 4096 byte copy, 1,000,000 times (GCC 13.3 `-O2`, three functions kept out of line; the checksums printed by the program are equal, so all three copy the same bytes):

```text
T1 copy of 4096 bytes, 1000000 times: checksum loop 127503328, duff 127503328, memcpy 127503328
```

| Copy of 4096 bytes, 1,000,000 times | ms (run 1 / run 2) |
|---|---|
| plain byte loop | 757 / 739 |
| Duff's device | 887 / 810 |
| `memcpy` | 28 / 28 |

Duff's device was **slower** than the plain loop here, by about 10 to 20 percent, and both were about 26 to 32 times slower than `memcpy`. The device was designed for compilers that did not unroll loops by themselves. I did not read the code GCC generated, so I cannot say why the device loses here. The interview answer: it is a clever use of the `switch` syntax, it is legal C, and you would not use it today; measure before unrolling by hand (which is also what Tom Duff says). The `memcpy` figure is a 4 KB block that stays in the first level cache, so it is the best case for a library routine.

| Item | Cost | Evidence |
|---|---|---|
| Ring buffer put or get | One AND, one compare, no division | section 5.1 |
| SPSC ring, two threads, one byte per call | About 20 to 25 ns per byte through the ring on this machine | T2 |
| Linked list reverse, cycle test, middle | One pass, O(1) extra space | section 5.2 |
| Pool allocate and free | O(1), no search; the `used` byte per block buys the double free check | section 5.3 |
| Duff's device | 10 to 20 percent slower than a plain loop; 26 to 32 times slower than `memcpy` | T1 |
| `volatile` access | A real load or store every time; prevents caching in a register | section 6.1 |
| Watchdog | One kick per main loop pass | section 6.3 |

---

## Common mistakes

1. Answering "what does this print" for undefined code with a confident output. Say "undefined" and give the rule.
2. Forgetting the integer promotions and the usual arithmetic conversions: `unsigned char` sums, `-1 < 0u`, `sizeof(int) > -1`.
3. Writing `sizeof` of an array parameter, or `sizeof(p) / sizeof(p[0])` on a pointer.
4. `malloc(strlen(s))` with no `+ 1`.
5. Using `==` on strings, or `memcmp` on structs with padding.
6. An unsigned loop variable with `>= 0`.
7. `char c = getchar()` and comparing with `EOF`.
8. A macro with unparenthesised arguments, a macro with side-effect arguments, or a multi-statement macro without `do { } while (0)`.
9. Relying on the order in which function arguments or operands are evaluated.
10. Reducing ring buffer indices modulo the capacity, so that "full" and "empty" cannot be told apart.
11. A power-of-two ring buffer with a capacity that is not a power of two (the mask is then wrong).
12. A linked-list removal that special-cases the head with its own code path, when a pointer to the pointer removes the case; `free(head); head = head->next;`.
13. Merging sorted lists with `<=`, which is not stable.
14. A pool allocator that does not reject a foreign or double-freed pointer.
15. A flag shared with an interrupt handler that is not `volatile` (or atomic): the loop works at `-O0` and hangs at `-O2`.
16. Clearing a write-1-to-clear flag with `SR |= FLAG`.
17. Kicking the watchdog from a timer interrupt, so that a hung main loop is never detected.
18. Spinning on a status flag without a timeout.
19. Trusting a single-threaded test, or a strongly ordered machine, as proof that a lock-free structure is correct.

---

## Questions out loud

1. What are the four classes of behaviour in the C standard, and which is "the order of the arguments in a call"?
2. Why is `-1 < 0u` false? State the rule.
3. What does `sizeof` give for an array in the caller and for the same array as a parameter, and why?
4. What is the difference between `a`, `&a`, `a + 1` and `&a + 1` for `int a[4]`?
5. Why does a `const` local modified through a pointer print two different values at `-O0` and `-O2`?
6. How does Duff's device work, and would you use it today?
7. Write a macro for the maximum of two values. What goes wrong with `MAX(i++, j++)`, and what are the alternatives?
8. How do you tell a full ring buffer from an empty one without wasting a slot? What happens when the counters wrap?
9. Why must the ring buffer capacity be a power of two in the free-running counter design?
10. What is the memory-order argument for the lock-free single-producer, single-consumer ring?
11. How do you remove a node from a singly linked list without a special case for the head? Find the n-th node from the end in one pass.
12. How do you detect a cycle in a linked list, and why does Floyd's algorithm terminate?
13. What is the difference between `volatile` and `atomic`? What does `volatile` guarantee?
14. Why must an interrupt handler be short and non-blocking, and how does it share data with the main loop?
15. What is a write-1-to-clear register, and what goes wrong with `SR |= FLAG`?
16. Where should a watchdog be kicked, and why not from a timer interrupt?

---

## Problems

Five problems, in order of difficulty. **E (entry level)**, **M (medium)**, **H (hard)**.

**Where to solve them.** Any Linux machine with `gcc`; the programs include `../code/ds.h` and `../code/emb.h`, so make a `code` folder next to the folder you compile in and assemble the two headers from the regions printed in sections 5 and 6 (put each in an `#ifndef` guard with the standard includes: `stdatomic.h`, `stddef.h`, `stdint.h`, `stdlib.h`, `string.h`; `emb.h` also includes `ds.h`). Problem E needs `-pthread` and benefits from `-fsanitize=thread`. For A and B an online compiler such as Compiler Explorer is enough to see the output and the warnings; I did not test it for the sanitizer builds.

**A (E).** Predict the output of the eight lines below, then run it. Say which lines depend on implementation-defined behaviour and which are defined.

```c
#include <limits.h>
#include <stdio.h>

int main(void)
{
    printf("1: %d\n", (unsigned char)-1 == -1);
    printf("2: %d\n", 5 > 3 > 1);
    printf("3: %zu\n", sizeof(struct { char c; double d; }));
    int i = 2;
    printf("4: %d\n", i << 1 + 1);
    printf("5: %d %d\n", INT_MAX + 1u > 0, (long)INT_MAX + 1 > 0);
    char s[] = "abc";
    printf("6: %zu %zu\n", sizeof s, sizeof(s + 0));
    printf("7: %d\n", 'a' + 'b' == 195);
    printf("8: %d\n", 10 / 4 * 4 + 10 % 4);
    return 0;
}
```

*How to approach it.* Work line by line with the checklist of section 1: types first (which operand is converted, and to what), then precedence, then evaluate. Line 2 is about how the comparison operators associate; line 4 about the precedence of `<<` against `+`.

**B (E).** The ring buffer below is the common first attempt. Run it, say what is wrong with it (two things), and fix it using free-running counters. Your version must accept exactly CAP items, refuse the rest, and be usable again after it has been drained.

```c
#include <stdio.h>

/* A ring buffer of ints, written the way many first attempts are. */
#define CAP 4
typedef struct { int buf[CAP]; unsigned head, tail; } Ring;

static void put(Ring *r, int v)
{
    r->buf[r->head] = v;
    r->head = (r->head + 1) % CAP;
}

static int get(Ring *r)
{
    int v = r->buf[r->tail];
    r->tail = (r->tail + 1) % CAP;
    return v;
}

static int empty(const Ring *r) { return r->head == r->tail; }
static int full(const Ring *r) { return (r->head + 1) % CAP == r->tail; }

int main(void)
{
    Ring r = {{0}, 0, 0};
    int stored = 0;
    for (int i = 1; i <= CAP; ++i) {
        if (!full(&r)) {
            put(&r, i);
            stored++;
        }
    }
    printf("capacity %d, stored %d\n", CAP, stored);
    for (int i = 5; i <= 8; ++i)
        put(&r, i);                               /* the caller forgot to check full() */
    printf("empty() says %d after 8 puts and no gets\n", empty(&r));
    return 0;
}
```

*How to approach it.* The program tells you: after CAP puts it says it stored only CAP minus one, and `empty()` is true after 8 puts. Ask what state `head == tail` stands for, and why the "full" test wastes a slot. Then change the indices to counters that are never reduced and take the remainder only when you index the array.

**C (M).** Using `Node` and `list_push` from section 5.2, write `list_remove_all(Node **link, int v)` (removes and frees every node with the value `v`, returns how many) and `list_insert_sorted(Node **link, int v, int tag)` (insert after the existing nodes with an equal value). Neither may special-case the head. Test them on the values 5, 1, 4, 1, 3, 1, 9.

*How to approach it.* A pointer to the pointer that points at the current node is the whole trick: to delete, write `*link = (*link)->next`; to advance, `link = &(*link)->next`. For the insert, advance while `(*link)->v <= v`, then splice with `list_push(*link, ...)` and store the result in `*link`. Check an empty list, a one-element list and removing the head and the tail.

**D (M).** Extend the pool of section 5.3 with `pool_calloc` (a zeroed block) and `pool_report` (print the indices of the blocks in use and the number of free blocks). Then use them in a program that allocates three blocks, frees one, frees it again, frees a pointer one byte inside a block, allocates again and frees everything. Say what the second call to `pool_free` and the third must return, and which block `pool_alloc` hands out next.

*How to approach it.* You do not need new state: the `used` array already knows which blocks are in use. The free list is last in, first out, so the block freed last is the one handed out next. Evaluate the three `pool_free` calls into separate variables before printing: the order in which the arguments of one `printf` are evaluated is unspecified (Q28), and a first version of this exercise got it wrong.

**E (H).** Write a lock-free single-producer, single-consumer ring of `uint32_t` with **bulk** write and read: `q32_write(q, src, n)` and `q32_read(q, dst, n)` return how many words were transferred, never block, and copy in at most two `memcpy` calls. Test it with a producer thread that sends the numbers 0 to N-1 in random batches of 1 to 100 and a consumer that receives in random batches of 1 to 100 and checks every number, and run the test under ThreadSanitizer.

*How to approach it.* Start from `Spsc` in section 5.1. The producer reads `tail` with acquire and its own `head` relaxed, computes the space, clamps `n`, copies in up to two pieces (to the end of the array, then from its start), and publishes **once** with a release store of `head + n` after both copies. The consumer is the mirror image. The only difference from the single-byte version is that the publication happens after the copy of the whole batch. Check the wrap case by using a ring much smaller than the total.

---

## Answers

Machine check of all five answers (each line is a script that ran the programs below and tested their output):

```text
problem A: ok (8 lines of output)
problem B: ok (bug shown, fixed twin correct)
problem C: ok (list operations)
problem D: ok (pool errors and leak report)
problem E: ok (threaded run clean, TSan 0 warnings)
```

**A.** The program printed:

```text
1: 0
2: 0
3: 16
4: 8
5: 1 1
6: 4 8
7: 1
8: 10
```

1. `(unsigned char)-1` is 255, promoted to `int`, compared with -1: 0 (defined).
2. `5 > 3 > 1` is `(5 > 3) > 1` = `1 > 1` = 0; GCC warns that comparison chains do not mean what mathematics means.
3. `char`, 7 bytes of padding, `double`: 16.
4. `i << 1 + 1` is `i << (1 + 1)` = 8, because `+` binds tighter than `<<`.
5. `INT_MAX + 1u` is unsigned, 2147483648 > 0 is true; `(long)INT_MAX + 1` is a `long` with no overflow: 1 and 1.
6. 4 (the array) and 8 (`s + 0` is a pointer).
7. `'a' + 'b'` is 97 + 98 = 195 on ASCII: 1 (the character set is implementation-defined, but the question is about ASCII).
8. `10 / 4 * 4 + 10 % 4` = `2 * 4 + 2` = 10: the identity `(a / b) * b + a % b == a`.

None of the eight lines is implementation-defined in the sense of Q10 except line 7, which assumes ASCII; the rest are defined.

**B.** The bugs: the "full" test `(head + 1) % CAP == tail` means the buffer holds at most CAP minus one items, and `head == tail` means empty, so a caller that does not check `full()` (as the second loop does) laps the ring and `empty()` says false while data was overwritten:

```text
capacity 4, stored 3
empty() says 0 after 8 puts and no gets
```

The version with free-running counters (CAP is a power of two) stores 4 of 8, refuses the rest, and accepts again once drained:

```c
#include <stdio.h>
#include <stdint.h>

#define CAP 4
typedef struct { int buf[CAP]; uint32_t head, tail; } Ring;     /* CAP is a power of two */

static int put(Ring *r, int v)
{
    if (r->head - r->tail == CAP)
        return -1;
    r->buf[r->head++ % CAP] = v;
    return 0;
}

static int get(Ring *r, int *v)
{
    if (r->head == r->tail)
        return -1;
    *v = r->buf[r->tail++ % CAP];
    return 0;
}

int main(void)
{
    Ring r = {{0}, 0, 0};
    int stored = 0;
    for (int i = 1; i <= 8; ++i)
        stored += put(&r, i) == 0;
    int v, sum = 0;
    while (get(&r, &v) == 0)
        sum += v;
    printf("capacity %d, stored %d of 8, sum of what came out %d, full again after draining: %s\n", CAP, stored, sum,
           put(&r, 9) == 0 ? "no, it accepts again" : "stuck");
    return 0;
}
```

```text
capacity 4, stored 4 of 8, sum of what came out 10, full again after draining: no, it accepts again
```

`put` and `get` now return an error code instead of corrupting the data. The sum 10 is 1 + 2 + 3 + 4: exactly the first four items, and the other four were refused, not overwritten.

**C.**

```c
#include <stdio.h>
#include "../code/ds.h"

/* Remove every node whose value is v. The link is a pointer to the pointer that points at the node,
   so the head needs no special case. Returns the number removed. */
static int list_remove_all(Node **link, int v)
{
    int removed = 0;
    while (*link) {
        if ((*link)->v == v) {
            Node *dead = *link;
            *link = dead->next;
            free(dead);
            removed++;
        } else {
            link = &(*link)->next;
        }
    }
    return removed;
}

/* Insert in sorted position; a new node goes after the existing nodes with an equal value. */
static void list_insert_sorted(Node **link, int v, int tag)
{
    while (*link && (*link)->v <= v)
        link = &(*link)->next;
    Node *n = list_push(*link, v, tag);
    *link = n;
}

static void show(const char *label, const Node *h)
{
    printf("%-26s", label);
    for (; h; h = h->next)
        printf(" %d", h->v);
    printf("\n");
}

int main(void)
{
    Node *h = NULL;
    int vals[] = {5, 1, 4, 1, 3, 1, 9};
    for (int i = 0; i < 7; ++i)
        list_insert_sorted(&h, vals[i], i);
    show("sorted insert of 7 values:", h);
    int n = list_remove_all(&h, 1);
    printf("removed %d nodes with value 1\n", n);
    show("after remove_all(1):", h);
    list_remove_all(&h, 9);
    show("after remove_all(9):", h);
    list_remove_all(&h, 3);
    show("after remove_all(3):", h);
    h = list_remove_nth_from_end(h, 1);
    show("after removing the last:", h);
    list_free(h);
    return 0;
}
```

```text
sorted insert of 7 values: 1 1 1 3 4 5 9
removed 3 nodes with value 1
after remove_all(1):       3 4 5 9
after remove_all(9):       3 4 5
after remove_all(3):       4 5
after removing the last:   4
```

`list_remove_all` takes the address of the head pointer, so removing the first node and removing an interior node use the same line. In `list_insert_sorted` the loop condition `<=` puts a new node after equal values (stable). The sorted insertion of 5, 1, 4, 1, 3, 1, 9 gave 1 1 1 3 4 5 9, removing the 1s left 3 4 5 9, and the removals then left 3 4 5 (without 9), 4 5 (without 3), and finally `list_remove_nth_from_end(h, 1)` removed the last node (the 5), leaving 4.

**D.**

```c
#include <stdio.h>
#include "../code/ds.h"

/* Leak report: the indices of the blocks that are still allocated. */
static void pool_report(const Pool *p)
{
    printf("in use:");
    int any = 0;
    for (size_t i = 0; i < p->count; ++i)
        if (p->used[i]) {
            printf(" %zu", i);
            any = 1;
        }
    printf("%s (%zu of %zu free)\n", any ? "" : " none", p->nfree, p->count);
}

static void *pool_calloc(Pool *p)
{
    void *b = pool_alloc(p);
    if (b)
        memset(b, 0, p->block);
    return b;
}

int main(void)
{
    static uint8_t mem[200];
    uint8_t used[16];
    Pool p;
    size_t n = pool_init(&p, mem, sizeof mem, 20, used);
    printf("200 byte buffer, block 20 rounded to %zu: %zu blocks\n", p.block, n);
    void *a = pool_alloc(&p), *b = pool_calloc(&p), *c = pool_alloc(&p);
    pool_report(&p);
    int r1 = pool_free(&p, b);
    int r2 = pool_free(&p, b);
    int r3 = pool_free(&p, (uint8_t *)a + 1);
    printf("free(b) = %d, free(b) again = %d, free(a + 1) = %d\n", r1, r2, r3);
    pool_report(&p);
    void *d = pool_alloc(&p);
    printf("the block freed last is handed out first: %s\n", d == b ? "yes" : "no");
    pool_free(&p, a);
    pool_free(&p, c);
    pool_free(&p, d);
    pool_report(&p);
    return 0;
}
```

```text
200 byte buffer, block 20 rounded to 32: 6 blocks
in use: 0 1 2 (3 of 6 free)
free(b) = 0, free(b) again = -2, free(a + 1) = -1
in use: 0 2 (4 of 6 free)
the block freed last is handed out first: yes
in use: none (6 of 6 free)
```

The 200 byte buffer gives 6 blocks of 32 bytes (20 rounded up to a multiple of 16; the remaining bytes are lost to alignment and to the last partial block). The first `pool_free(b)` returns 0, the second -2 (double free), and the pointer one byte inside a block returns -1 (not the start of a block). The block freed last is allocated next because the free list is last in, first out. After freeing `a`, `c` and `d` the report says nothing is in use: a leak report is the same loop with the opposite sense.

**E.**

```c
#include <pthread.h>
#include <stdio.h>
#include "../code/ds.h"

#ifndef N
#define N 4000000u
#endif

typedef struct {
    uint32_t *buf;
    uint32_t mask;
    _Atomic uint32_t head, tail;
} Q32;

/* Producer side: copy up to n words in, in at most two pieces, then publish with one release store. */
static uint32_t q32_write(Q32 *q, const uint32_t *src, uint32_t n)
{
    uint32_t head = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&q->tail, memory_order_acquire);
    uint32_t space = q->mask + 1 - (head - tail);
    if (n > space)
        n = space;
    uint32_t pos = head & q->mask, first = q->mask + 1 - pos;
    if (first > n)
        first = n;
    memcpy(q->buf + pos, src, first * sizeof *src);
    memcpy(q->buf, src + first, (n - first) * sizeof *src);
    atomic_store_explicit(&q->head, head + n, memory_order_release);
    return n;
}

/* Consumer side, the mirror image. */
static uint32_t q32_read(Q32 *q, uint32_t *dst, uint32_t n)
{
    uint32_t tail = atomic_load_explicit(&q->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&q->head, memory_order_acquire);
    uint32_t avail = head - tail;
    if (n > avail)
        n = avail;
    uint32_t pos = tail & q->mask, first = q->mask + 1 - pos;
    if (first > n)
        first = n;
    memcpy(dst, q->buf + pos, first * sizeof *dst);
    memcpy(dst + first, q->buf, (n - first) * sizeof *dst);
    atomic_store_explicit(&q->tail, tail + n, memory_order_release);
    return n;
}

static uint32_t storage[256];
static Q32 q = { storage, 255, 0, 0 };
static unsigned bad;

static uint32_t next_rand(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s >> 16; }

static void *producer(void *arg)
{
    (void)arg;
    uint32_t seed = 1, sent = 0, batch[100];
    while (sent < N) {
        uint32_t want = 1 + next_rand(&seed) % 100;
        if (want > N - sent)
            want = N - sent;
        for (uint32_t i = 0; i < want; ++i)
            batch[i] = sent + i;
        uint32_t put = 0;
        while (put < want)
            put += q32_write(&q, batch + put, want - put);
        sent += want;
    }
    return NULL;
}

static void *consumer(void *arg)
{
    (void)arg;
    uint32_t seed = 7, got = 0, batch[100];
    while (got < N) {
        uint32_t n = q32_read(&q, batch, 1 + next_rand(&seed) % 100);
        for (uint32_t i = 0; i < n; ++i)
            if (batch[i] != got + i)
                bad++;
        got += n;
    }
    return NULL;
}

int main(void)
{
    pthread_t p, c;
    pthread_create(&p, NULL, producer, NULL);
    pthread_create(&c, NULL, consumer, NULL);
    pthread_join(p, NULL);
    pthread_join(c, NULL);
    printf("%u words in random batches of 1 to 100 through a 256 word ring, %u wrong\n", N, bad);
    return bad != 0;
}
```

```text
4000000 words in random batches of 1 to 100 through a 256 word ring, 0 wrong
ThreadSanitizer run (400000 words): 0 warnings
```

The 4,000,000 numbers arrived in order through a 256 word ring in random batches, and the run under ThreadSanitizer (400,000 words) reported no warnings. Both `memcpy` calls happen **before** the single release store of `head`, so the consumer, which reads `head` with acquire, can never see a count that covers words not yet written. The producer's `tail` load is acquire so that it does not reuse a slot the consumer is still reading; the consumer's `tail` store is release for the same reason. A producer that publishes `head` after the first `memcpy` only would be a bug that a strongly ordered machine hides.

---

## Cheat sheet

- Classify before you answer: defined, implementation-defined, unspecified, undefined. Undefined: signed overflow, unsequenced modifications (`i++ + ++i`), writing to a literal or to a `const` object, out-of-range shifts, uninitialised reads. Unspecified: the order of function arguments (GCC 3 2 1, clang 1 2 3). Implementation-defined: `char` signedness, out-of-range conversion to a signed type, `>>` of a negative value.
- Types: promote anything narrower than `int`; if signedness differs and the unsigned type is at least as wide, the signed operand becomes unsigned (`-1 < 0u` is false). `sizeof` has type `size_t`. A character constant is an `int`.
- Arrays: a parameter `int a[10]` is a pointer. `a` against `&a`: `a + 1` moves one element, `&a + 1` moves the whole array. `a[i]` is `*(a + i)`. Partial initialisers zero the rest.
- Macros: parenthesise parameters and the result, never pass side effects, `do { } while (0)` for statements. `#` does not expand its operand; `##` pastes.
- Ring buffer: power-of-two capacity, free-running counters, index `& mask`, fill `head - tail`, no wasted slot, wrap-safe in unsigned arithmetic, bulk copy in two pieces. SPSC: each side writes its own counter, release on publish, acquire on read.
- Linked list: three-pointer reverse, Floyd for cycles and the middle, two pointers `n` apart for the n-th from the end, a pointer to the pointer to avoid the head special case, a stable merge uses `<`.
- Fixed-block pool: the free list inside the free blocks, block size rounded to the alignment, `used` flags to reject foreign pointers and double frees, no fragmentation, not thread safe.
- `volatile`: every access happens; use for registers and handler flags (`volatile sig_atomic_t`); not atomic, not an inter-thread barrier. A non-volatile flag hangs at `-O2`.
- ISR: short, no allocation, no printing, no blocking, acknowledge the device, share data through an atomic or volatile object or a lock-free ring.
- Write-1-to-clear: write only the bit you handled; `SR |= FLAG` clears every pending flag.
- Watchdog: kick from the main loop only after every required task has checked in; never from a timer interrupt; record the reset cause.
- Duff's device: legal C, measured slower than a plain loop on GCC 13.3, and 26 to 32 times slower than `memcpy`.

---

## Sources

- Evaluation order, sequence points and unsequenced modifications, checked ([cppreference, order of evaluation](https://en.cppreference.com/w/c/language/eval_order)).
- Integer promotions, the usual arithmetic conversions and out-of-range conversion to a signed type (implementation-defined), checked ([cppreference, implicit conversions](https://en.cppreference.com/w/c/language/conversion)).
- What `volatile` guarantees and does not guarantee, checked ([cppreference, volatile](https://en.cppreference.com/w/c/language/volatile)).
- Duff's device and Tom Duff's own remarks, checked ([Tom Duff's note](https://www.lysator.liu.se/c/duffs-device.html)).
- ThreadSanitizer, `-fsanitize=thread`, detects data races, checked ([Clang ThreadSanitizer](https://clang.llvm.org/docs/ThreadSanitizer.html)).
- Not checked: the behaviour of ARM, AVR and other targets, memory barriers on ARM, and clang beyond the cases measured in the text (all marked "from memory").
- C 03 (pool allocators), C 04 (layout, `offsetof`), C 07 (`memmove`, `atoi`, `strtok`), C 10 (atomics, signals, `__attribute__`) and C 11 (sanitizers, fuzzing, MISRA in outline). The Embedded track and the Drivers track take the register-level material further.
