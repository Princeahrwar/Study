# C 10: Advanced C Features and Error Handling

*Part of the [C language plan](00_plan.md) · Time: about 6 hours (4 reading, 2 problems) · You need: [C 03](03_memory_stack_heap_and_malloc.md) for the heap and the sanitizers, [C 05](05_qualifiers_storage_classes_and_linkage.md) for `volatile` and linkage, [C 06](06_preprocessor_compilation_and_linking.md) for macros, [C 08](08_advanced_pointers_and_pointer_bugs.md) for function pointers, and [C 09](09_struct_design_and_oop_in_c.md) for ownership. There is no companion demo file: every program below was compiled and run, and its real output is printed under it*

This tutorial is the toolbox that separates code that works from code that survives production: functions with a variable number of arguments, the conventions for reporting and cleaning up after errors, non-local jumps with `setjmp`, type-generic macros, the C11 atomics and threads that the operating system tracks (OS 06 to OS 08) are built on, signals, the GCC extensions the Linux kernel is written in, floating point, and what C23 changed. Most of the lessons come from things that went wrong in the experiments: a variadic call that printed `4.52606e-316`, a `volatile` that changed a result, a flag loop that never ended, and a `-ffast-math` that deleted a NaN check.

By the end you can:

- write variadic functions with `va_list` and `va_copy`, and make the compiler check the calls with `__attribute__((format))`;
- explain argument promotion and why `va_arg(ap, float)` is wrong;
- choose between return codes, `errno`, out-parameters and error enums, and parse a number completely and safely with `strtol`;
- write cleanup code three ways (nested `if`, `goto` ladder, the `cleanup` attribute) and say when each is right;
- implement try and catch with `setjmp` and `longjmp`, and say what `volatile` is for and what `longjmp` does not undo;
- write `_Generic` macros, say what the controlling expression's type is, and avoid the trap where every branch is type checked;
- use `_Static_assert`, `_Alignas`, `ARRAY_SIZE`, `BUILD_BUG_ON` and a type-safe `min` and `max`;
- use variable length arrays and pointers to them, and say what they risk;
- use C11 atomics and threads: atomic counters, a spinlock, release and acquire publication, a lock free ring buffer, and false sharing;
- write signal handlers that only do what is async-signal-safe;
- use the GCC attributes and builtins the kernel relies on, and measure `likely` and `unlikely`;
- predict the surprises of floating point and say what `-ffast-math` licenses;
- say what C23 adds and which parts GCC 13.3 accepts.

**Where the numbers come from.** Every program and command was run on an x86-64 Linux container (Ubuntu, glibc 2.39, GCC 13.3, 2 vCPUs). Code is compiled with `-std=gnu11 -Wall -Wextra -Wno-unused-function` unless the text says otherwise. The helper code printed in this tutorial (the variadic string builder and logger, the try and catch macros, the three cleanup styles, a strict number parser, `ARRAY_SIZE`, `min`, `max`, `TYPE_NAME` and `abs_g`, and a lock free ring buffer) is the real source and was tested against oracles: 2,920,058 checks passed under AddressSanitizer and UBSan, comparing each part with a plain reference (`snprintf` into a large buffer, a closed form for the nested exceptions, an independent 128-bit parser, a failing allocator with a leak count, a model queue, and a two-thread producer and consumer). I tried 29 deliberate bugs in it (a missing `va_copy`, a `THROW` that does not pop its handler, a goto ladder that skips a `free`, a parser that ignores `ERANGE`, a ring buffer that never reports full, and others). 26 were caught: 13 by a failed check, 10 by the sanitizers or a crash, 2 by hanging until a timeout stopped them, and 1 by a compile error. Three escaped: one is equivalent (a parser line that is redundant because the next test covers the same case), and two weaken a memory ordering (`release` to `relaxed` on the producer, `acquire` to `relaxed` on the consumer) in a way that x86 hardware cannot show, so a test on this machine cannot fail. Section 7 shows that ThreadSanitizer does see the first of them. Sanitizer output is shortened: the process id, addresses, and frames from the C library and the sanitizer are removed, and the rest is verbatim. Statements about ARM64, Clang, musl, the Linux kernel and exact glibc internals are from memory and marked.

**Data and timing.** Machine: a cloud container, Intel Xeon 2.10 GHz, GCC 13.3.0, `-O2`, two vCPUs. Timings are the best of 3 wall-clock runs in milliseconds, shown as run 1 / run 2 (two complete repetitions of the whole benchmark). The loop is timed inside the program where that matters. They show the shape of the cost on this machine and will differ on yours, especially the threaded ones: a cloud VM does not tell you how its vCPUs map to physical cores.

---

## 1. Variadic functions

A function such as `printf` takes `...` after its named parameters. The callee reads the extra arguments with the macros of `<stdarg.h>`: `va_start(ap, last)` starts after the last named parameter, `va_arg(ap, type)` fetches the next argument **as that type**, `va_end(ap)` finishes. Two facts cause most of the bugs:

1. **The callee cannot tell how many arguments there are or what their types are.** The caller must say: a count (`sum_n(3, ...)`), a sentinel, or a format string.
2. **Arguments in the `...` part go through the default promotions:** `char` and `short` become `int`, and `float` becomes `double`. So `va_arg(ap, float)` and `va_arg(ap, char)` are wrong, and the compiler says so:

```text
d1w_float.c:7:27: warning: 'float' is promoted to 'double' when passed through '...'
d1w_float.c:7:27: note: (so you should pass 'double' not 'float' to 'va_arg')
d1w_float.c:7:27: note: if this code is reached, the program will abort
```

("The program will abort" is GCC's description of what it does with that code: it inserts a trap.) The type must also match **exactly in kind**. Here is a function that averages doubles, called once with integer arguments by mistake:

```c
#include <stdarg.h>
#include <stdio.h>

static double avg(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    double total = 0;
    for (int i = 0; i < n; ++i)
        total += va_arg(ap, double);
    va_end(ap);
    return total / n;
}

int main(void)
{
    printf("avg(3, 1, 2, 3)       = %g\n", avg(3, 1, 2, 3));
    printf("avg(3, 1.0, 2.0, 3.0) = %g\n", avg(3, 1.0, 2.0, 3.0));
    return 0;
}
```

```text
avg(3, 1, 2, 3)       = 4.52606e-316
avg(3, 1.0, 2.0, 3.0) = 2
```

The first call passed `int`s where `va_arg(ap, double)` expected doubles, so it read floating point registers that nobody set: the result is `4.52606e-316`, a denormal garbage value, and the call is undefined behaviour. The second call is correct. On x86-64 System V, integer and floating point arguments travel in different registers, which is why this prints garbage instead of crashing (from memory). Variadic misuse does not crash politely.

**The safe building blocks.** The first helper is a printf-style function that returns a malloc'ed string. It needs to run the format twice, once to measure and once to write, and a `va_list` can only be consumed once, so it copies the list with `va_copy`:

```c
/* A printf-style function that returns a malloc'ed string. The format attribute makes the
   compiler check the arguments of every call against the format string. */
static char *xasprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static char *vxasprintf(const char *fmt, va_list ap)
{
    va_list ap2;
    va_copy(ap2, ap);                       /* the first pass consumes ap2, the second pass needs ap */
    int n = vsnprintf(NULL, 0, fmt, ap2);   /* measure */
    va_end(ap2);
    if (n < 0)
        return NULL;
    char *p = malloc((size_t)n + 1);
    if (!p)
        return NULL;
    vsnprintf(p, (size_t)n + 1, fmt, ap);   /* write */
    return p;
}
static char *xasprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *p = vxasprintf(fmt, ap);
    va_end(ap);
    return p;
}

/* The count is passed explicitly: a variadic function cannot find out how many arguments it got. */
static long sum_n(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    long s = 0;
    for (int i = 0; i < n; ++i)
        s += va_arg(ap, int);
    va_end(ap);
    return s;
}

/* Safer: let the macro count the arguments with a compound literal. */
static long sum_arr(const int *v, size_t n)
{
    long s = 0;
    for (size_t i = 0; i < n; ++i)
        s += v[i];
    return s;
}
#define SUM(...) sum_arr((int[]){__VA_ARGS__}, sizeof((int[]){__VA_ARGS__}) / sizeof(int))

/* A logger in the snprintf style: prefix with the level, then the message, truncating safely. */
static int log_to(char *buf, size_t size, int level, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static int log_to(char *buf, size_t size, int level, const char *fmt, ...)
{
    int head = snprintf(buf, size, "[L%d] ", level);
    if (head < 0)
        return -1;
    size_t off = (size_t)head < size ? (size_t)head : size;
    va_list ap;
    va_start(ap, fmt);
    int body = vsnprintf(buf + off, size - off, fmt, ap);
    va_end(ap);
    return body < 0 ? -1 : head + body;
}
```

```text
cart has 3 items costing 19.50
sum_n(3, 10, 20, 30) = 60
SUM(1, 2, 3, 4, 5) = 15   (the macro counted 5 arguments)
log_to into 16 bytes: "[L2] disk 93% f", full length 31
```

- **`va_copy`** (C99) makes a second list from the first. Without it the measuring pass consumes the only list and the writing pass reads garbage: the mutant that removed it was caught by the sanitizers. Every `va_copy` needs a matching `va_end`.
- **`__attribute__((format(printf, 1, 2)))`** tells GCC that argument 1 is a printf format and the arguments to check start at argument 2. With it the compiler checks every call, as it does for `printf` itself:

```text
d1f_format.c:6:27: warning: format '%d' expects argument of type 'int', but argument 2 has type 'char *' [-Wformat=]
```

  Put it on every printf-like function you write, and compile with `-Wformat=2` for the warnings about non-literal formats. In the kernel it is on `printk` and every logging helper.
- **`SUM(...)` is the safe replacement for a count argument.** The macro builds a compound literal array from its arguments, and `sizeof` divided by the element size is the count, computed by the compiler. The count can no longer disagree with the arguments, and the arguments are converted to `int`, so a wrong type is a compile time conversion and not a run time misread. Use this pattern wherever the arguments have one type.
- **`log_to` follows the `snprintf` contract.** It writes a prefix, then the message through `vsnprintf` at the right offset, never writes past the buffer, always terminates it, and returns the length the complete text would have. The output shows a 31 character line cut to 15 characters plus the terminator, with the full length returned so the caller knows it was truncated. The mutant that did not clamp the offset to the buffer size was caught.

Rules of thumb: a variadic function needs at least one named parameter; prefer a macro over a count when the arguments have one type; always provide a `v` version (`vxasprintf`) next to the `...` version so wrappers can forward the arguments; and use `__builtin_va_arg_pack` or macros when you need to forward `...` directly (from memory, GCC specific).

---

## 2. Error handling conventions

C has no exceptions, so each library chooses a convention, and the skill is to pick one per layer and to be consistent. The common ones, in the order you meet them:

| Convention | Example | Strength | Weakness |
|---|---|---|---|
| Return a status, results through pointers | `int parse(const char *s, long *out)` | Cannot be confused with a valid result; the compiler can warn on an ignored result (`warn_unused_result`) | Out-parameters are clumsy; callers still ignore the status |
| Return a sentinel value | `malloc` returns NULL, `fopen` returns NULL, `read` returns -1 | One return value | The sentinel must be outside the valid range; says nothing about *why* |
| Return -1 and set `errno` | `open`, `read`, `strtol` (on overflow) | Standard with POSIX, carries the reason | `errno` is global state: it is only meaningful after a call that failed, and any later call can overwrite it |
| Return the negative error code | Linux kernel: `return -ENOMEM;` | Value and reason in one number, no global state | Mixes with valid negative results unless the range is reserved |
| An error enum | `ParseErr` below | Self documenting, switchable, compiler can check every case | Needs a conversion to text |
| A sticky error flag in the object | `ferror(f)`, error state in a parser | Callers check once at the end | Delays the report; the failure site is lost |

**`errno`** is a thread-local integer (in glibc a macro that expands to a per-thread location). Four rules: **set `errno = 0` before a call whose only error signal is `errno`** (`strtol`, `strtod`), **read it immediately** after the failure and before any other call that might change it, **never test `errno` unless the function documented a failure**, and **save it if you must call something in between** (a cleanup function that calls `free` may change it). It is per thread, as this program shows:

```c
#include <errno.h>
#include <stdio.h>
#include <threads.h>

static int worker(void *arg)
{
    int mine = *(int *)arg;
    errno = mine;
    for (volatile int i = 0; i < 1000000; ++i)
        ;
    return errno == mine ? 0 : 1;
}

int main(void)
{
    errno = 7;
    thrd_t t[2];
    int codes[2] = {11, 22};
    for (int i = 0; i < 2; ++i)
        thrd_create(&t[i], worker, &codes[i]);
    int bad = 0, r;
    for (int i = 0; i < 2; ++i) {
        thrd_join(t[i], &r);
        bad += r;
    }
    printf("each thread kept its own errno: %s; main's errno is still %d\n", bad == 0 ? "yes" : "no", errno);
    return 0;
}
```

```text
each thread kept its own errno: yes; main's errno is still 7
```

**A strict number parser.** `atoi` returns 0 for garbage and cannot report overflow, and `strtol` signals failure in three different ways (no digits, trailing text, `ERANGE`) and each is a separate way to forget a check. The function below checks them all and returns an enum, so each outcome has a name:

```c
typedef enum { PARSE_OK = 0, PARSE_EMPTY, PARSE_SYNTAX, PARSE_RANGE } ParseErr;

/* Parse a whole string as a decimal integer in [lo, hi]. No leading space, no trailing text. */
static ParseErr parse_long(const char *s, long lo, long hi, long *out)
{
    if (!s || !*s)
        return PARSE_EMPTY;
    if (isspace((unsigned char)*s))
        return PARSE_SYNTAX;
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0')
        return PARSE_SYNTAX;
    if (errno == ERANGE || v < lo || v > hi)
        return PARSE_RANGE;
    *out = v;
    return PARSE_OK;
}

static const char *parse_err_str(ParseErr e)
{
    switch (e) {
    case PARSE_OK: return "ok";
    case PARSE_EMPTY: return "empty";
    case PARSE_SYNTAX: return "not a number";
    case PARSE_RANGE: return "out of range";
    }
    return "?";
}
```

It rejects the empty string, leading whitespace (which `strtol` would skip), trailing characters, a lone sign, overflow of `long`, and values outside the caller's range `[lo, hi]`, and it leaves `*out` unchanged on every failure. It was compared with an independent 128-bit reference on 200,000 random and boundary strings (`""`, `"+"`, `"0x10"`, `"9223372036854775808"`, `"-9223372036854775809"` and others). The mutants that ignored `ERANGE`, accepted trailing text, accepted leading whitespace, and excluded the lower bound were all caught. One mutant escaped: removing the test `end == s`. It is **equivalent**, because the next test `*end != '\0'` already rejects every string that has no digits (the string is non-empty, so with no digits `end` is the start and `*end` is the first character, never the terminator). That is a useful reminder that a redundant check is safe to remove only when you can say which other check covers it.

Design rules that scale: **every function that can fail says how in its header comment**; **the caller handles or propagates every error** (a `warn_unused_result` attribute on the declaration turns an ignored status into a warning); **do not mix conventions inside one module**; and **keep the error value close to its cause** (return the specific code, translate at the boundary).

---

## 3. Cleaning up: nested `if`, `goto`, and the `cleanup` attribute

Section 8 of C 09 introduced the `goto` ladder for a constructor. Here is the same job written the three ways in use. Each function allocates a struct and three buffers and must release exactly what it acquired when any step fails. A counting allocator (`cmalloc` and `cfree`) lets a test fail the k-th allocation and check that the number of successful allocations equals the number of frees.

```c
/* A counting allocator so a test can make the k-th allocation fail and check for leaks. */
static size_t g_alloc, g_free, g_fail_at;
static void *cmalloc(size_t n)
{
    if (++g_alloc == g_fail_at)
        return NULL;
    return malloc(n);
}
static void cfree(void *p)
{
    if (p)
        g_free++;
    free(p);
}

typedef struct { char *a, *b, *c; } Triple;

static void triple_free(Triple *t)
{
    if (!t)
        return;
    cfree(t->c);
    cfree(t->b);
    cfree(t->a);
    cfree(t);
}

/* Style 1: nested ifs. The success path ends up deeply indented. */
static Triple *triple_nested(size_t n)
{
    Triple *t = cmalloc(sizeof *t);
    if (t) {
        t->a = cmalloc(n);
        if (t->a) {
            t->b = cmalloc(n);
            if (t->b) {
                t->c = cmalloc(n);
                if (t->c)
                    return t;
                cfree(t->b);
            }
            cfree(t->a);
        }
        cfree(t);
    }
    return NULL;
}

/* Style 2: goto cleanup, one label per resource, released in reverse order. */
static Triple *triple_goto(size_t n)
{
    Triple *t = cmalloc(sizeof *t);
    if (!t)
        goto out;
    t->a = cmalloc(n);
    if (!t->a)
        goto free_t;
    t->b = cmalloc(n);
    if (!t->b)
        goto free_a;
    t->c = cmalloc(n);
    if (!t->c)
        goto free_b;
    return t;

free_b:
    cfree(t->b);
free_a:
    cfree(t->a);
free_t:
    cfree(t);
out:
    return NULL;
}

/* Style 3: the GCC and Clang cleanup attribute runs a function when the variable leaves scope. */
static void autofree_(void *pp) { cfree(*(void **)pp); }
#define AUTOFREE __attribute__((cleanup(autofree_)))
#define STEAL(p) ({ __typeof__(p) s_ = (p); (p) = NULL; s_; })

static Triple *triple_cleanup(size_t n)
{
    AUTOFREE Triple *t = cmalloc(sizeof *t);
    if (!t)
        return NULL;
    t->a = t->b = t->c = NULL;
    AUTOFREE char *a = cmalloc(n);
    AUTOFREE char *b = cmalloc(n);
    AUTOFREE char *c = cmalloc(n);
    if (!a || !b || !c)
        return NULL;               /* every variable that holds memory is released on this path */
    t->a = STEAL(a);               /* success: ownership moves out, so the cleanup frees nothing */
    t->b = STEAL(b);
    t->c = STEAL(c);
    return STEAL(t);
}
```

- **Nested `if`**: no jumps, but the success path ends up three levels deep, and every failure branch must remember which resources to free. It stops scaling at about three resources. The mutant that forgot one `cfree(t->b)` was caught.
- **`goto` ladder**: a straight line of acquisitions, each followed by its failure test, and one label per resource released in reverse order. The cost is that the labels can be misordered: the mutant that jumped to `free_t` instead of `free_a` leaked a buffer and was caught by the leak counter. This is the style of the Linux kernel and most system code (from memory, a conventional style: the kernel coding style document endorses `goto` for centralised exit), and it handles any number of resources.
- **`cleanup` attribute** (GCC and Clang): `__attribute__((cleanup(f)))` on a local variable calls `f(&var)` when the variable goes out of scope, on **every** `return`. `AUTOFREE` frees the pointer automatically, and `STEAL` moves ownership out on success by clearing the variable, so the cleanup frees nothing. This is the closest C comes to destructors, and it is what systemd and GLib (`g_autoptr`) are built on (from memory). Two mutants test its rules: leaving out the `STEAL` of `c` freed `c` twice, and returning `t` without `STEAL(t)` returned a pointer that the cleanup had already freed. Both were caught by the sanitizers. Variables that hold memory must be initialised before any early `return`, because the cleanup runs on whatever the variable holds.

All three passed the same oracle for every failing allocation from the first to the fourth and for no failure. A summary of when to use which: nested `if` for one or two resources, `goto` when many resources or code shared with a project that already uses it, and `cleanup` when you want scope based release and the code only targets GCC or Clang.

---

## 4. `setjmp` and `longjmp`: non-local exits

`setjmp(env)` saves the execution context (stack pointer, program counter, callee-saved registers) in a `jmp_buf` and returns 0. A later `longjmp(env, v)` restores it and makes that `setjmp` **return again**, this time with the value `v` (never 0). It lets a deep function abandon its callers, which is C's nearest relative to an exception. It is used in `libpng` and `libjpeg` error handling and in the Lua interpreter (from memory).

The macros below give it the shape of try and catch, with a per-thread stack of handlers so tries nest:

```c
/* Exceptions from setjmp and longjmp: a per-thread stack of jump buffers. */
#define TRY_MAX 16
static _Thread_local struct {
    jmp_buf env[TRY_MAX];
    int depth;
} exc_;

#define TRY                                                  \
    do {                                                     \
        if (exc_.depth >= TRY_MAX)                           \
            abort();                                         \
        int code_ = setjmp(exc_.env[exc_.depth++]);          \
        if (code_ == 0) {
#define CATCH(c)                                             \
            exc_.depth--;           /* normal exit: pop */   \
        } else {                    /* THROW already popped */ \
            int c = code_;
#define END_TRY                                              \
        }                                                    \
    } while (0)

static void THROW(int code)
{
    if (exc_.depth == 0 || code == 0)
        abort();                    /* an uncaught exception, or code 0 which setjmp uses for "no throw" */
    int d = --exc_.depth;
    longjmp(exc_.env[d], code);
}
```

A user of the macros, with a function that rethrows a different code:

```c
#include <stdio.h>
#include "../code/adv.h"

static int parse_digit(char c)
{
    if (c < '0' || c > '9')
        THROW(1);
    return c - '0';
}

static int parse_pair(const char *s)
{
    int v = 0;
    TRY {
        v = parse_digit(s[0]) * 10 + parse_digit(s[1]);
    } CATCH(e) {
        printf("  inner catch: code %d for \"%s\", rethrowing as 2\n", e, s);
        THROW(2);
    } END_TRY;
    return v;
}

int main(void)
{
    const char *inputs[] = {"42", "7x", "99"};
    for (int i = 0; i < 3; ++i) {
        TRY {
            printf("parsing \"%s\" -> %d\n", inputs[i], parse_pair(inputs[i]));
        } CATCH(e) {
            printf("outer catch: code %d\n", e);
        } END_TRY;
    }
    return 0;
}
```

```text
parsing "42" -> 42
  inner catch: code 1 for "7x", rethrowing as 2
outer catch: code 2
parsing "99" -> 99
```

The inner `CATCH` caught code 1 from `"7x"`, rethrew code 2, and the outer `CATCH` received 2. The macros pass the oracle test on nested handlers up to 12 deep with random rethrow patterns, where the expected final code is computed from a closed form; the mutants that did not pop a handler either left the depth wrong (a hang) or jumped to the wrong frame (a crash).

**What `longjmp` does not do, and the rules that follow.**

1. **It does not run any cleanup.** No destructors exist in C, but the `cleanup` attribute of section 3 is also skipped:

```c
#include <setjmp.h>
#include <stdio.h>

static jmp_buf jb;
static void note(int *p) { printf("  cleanup ran for guard %d\n", *p); }

static void inner(void)
{
    __attribute__((cleanup(note))) int guard = 1;
    longjmp(jb, 1);                 /* leaves inner() without returning through its scope exit */
}

static void normal(void)
{
    __attribute__((cleanup(note))) int guard = 2;
}

int main(void)
{
    printf("a normal scope exit:\n");
    normal();
    printf("a longjmp out of the scope:\n");
    if (setjmp(jb) == 0)
        inner();
    printf("back in main\n");
    return 0;
}
```

```text
a normal scope exit:
  cleanup ran for guard 2
a longjmp out of the scope:
back in main
```

   A normal scope exit ran the cleanup and the `longjmp` did not. Memory allocated between `setjmp` and `longjmp` leaks, as LeakSanitizer shows for a function that allocates a buffer and throws before freeing it:

```text
caught 5, but work() never reached its free
ERROR: LeakSanitizer: detected memory leaks
Direct leak of 100 byte(s) in 1 object(s) allocated from:
    #1 work d2leak.c:7
    #2 main d2leak.c:16
    #3 __libc_start_call_main libc_start_call_main.h:58
    #4 __libc_start_main_impl ../csu/libc-start.c:360
SUMMARY: AddressSanitizer: 100 byte(s) leaked in 1 allocation(s).
```

   The program printed `caught 5` and still leaked the 100 bytes. **Every resource must be released by the code that catches, or tracked in a structure the handler can walk.**
2. **Local variables modified after `setjmp` have indeterminate values after the jump, unless they are `volatile`.** The compiler may keep a variable in a register, and `longjmp` restores the registers as they were at `setjmp` time. The standard says the value is indeterminate; GCC with `-Wclobbered` warns in some cases. The measurement:

```c
#include <setjmp.h>
#include <stdio.h>

static jmp_buf jb;

/* Counts how many times the body ran. Which variable is modified after setjmp decides everything. */
__attribute__((noinline)) static void bail(void) { longjmp(jb, 1); }

int main(void)
{
    int plain = 0;
    volatile int safe = 0;
    int rounds = 0;
    if (setjmp(jb) == 0) {
        plain = 42;
        safe = 42;
        rounds++;
        bail();
    }
    printf("plain = %d, volatile = %d\n", plain, safe);
    return 0;
}
```

```text
-O0: plain = 42, volatile = 42
-O1: plain = 0, volatile = 42
-O2: plain = 0, volatile = 42
-O3: plain = 0, volatile = 42
```

   At `-O0` both variables are 42, because both live in memory. From `-O1` up the plain variable reverts to 0 (the value at the time of `setjmp`) and only the `volatile` one keeps 42. The result **depends on the optimisation level**, which is the signature of undefined behaviour. A second program accumulates a value across the jumps:

```c
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

static jmp_buf jb;
__attribute__((noinline)) static void fail(int k) { if (k) longjmp(jb, 1); }

int main(int argc, char **argv)
{
    (void)argv;
    int tries = argc - 1;           /* 0 */
    int acc = 0;
    setjmp(jb);
    tries++;
    acc += tries * 10;
    if (tries < 3)
        fail(1);
    printf("tries = %d, acc = %d\n", tries, acc);
    return 0;
}
```

```text
-O0: tries = 3, acc = 60
-O1: tries = 3, acc = 30
-O2: tries = 3, acc = 30
-O3: tries = 3, acc = 30
```

   At `-O0` the sum is 60 (10 + 20 + 30); at `-O1` and above it is 30, because each jump rolled `acc` back to its value at `setjmp` time. The rule: **a variable that is changed between `setjmp` and `longjmp` and read after the jump must be `volatile`** (the `walk` function in the tests declares its `result` that way).
3. **Do not `return` out of a `TRY` body.** The macro pops its handler at the end of the body; a `return`, `break` or `goto` out of it skips the pop and leaves a stale handler on the stack. This is a limit of the macro style, and the reason many projects avoid it.
4. **Do not `longjmp` to a function that has already returned**, and do not jump into a different thread's context: the saved stack no longer exists or belongs to someone else. The handler stack above is `_Thread_local` for this reason.
5. **Signals:** `setjmp` may or may not save the signal mask; use `sigsetjmp(env, 1)` and `siglongjmp` when you jump out of a signal handler (from memory of POSIX).
6. **C++:** jumping over a C++ frame with a destructor is undefined behaviour, so a C library that uses `longjmp` for errors must be called with care from C++ (from memory).

When is it the right tool? For abandoning a deep recursion (a parser that hits a fatal syntax error), for a library error path where the alternative is checking a return value at every one of 50 levels, and for a coroutine style context switch in low level code. When the call chain is shallow, return codes with `goto cleanup` are easier to reason about.

---

## 5. Compile-time tools: `_Generic`, `_Static_assert`, alignment, and macro safety

**`_Generic`** (C11) chooses an expression by the **type** of its controlling expression, at compile time. The controlling expression is **not evaluated**; it is only typed. Here is a macro that names the type of its argument, with the answers for a set of expressions:

```c
#define TYPE_NAME(x) _Generic((x),                                \
        char: "char", signed char: "signed char", unsigned char: "unsigned char", \
        short: "short", unsigned short: "unsigned short",         \
        int: "int", unsigned: "unsigned", long: "long", unsigned long: "unsigned long", \
        long long: "long long", float: "float", double: "double", \
        char *: "char *", const char *: "const char *", void *: "void *", default: "other")

/* One name, the right function: the choice is made at compile time from the argument type. */
#define abs_g(x) _Generic((x), int: abs, long: labs, long long: llabs, float: fabsf, double: fabs)(x)
```

```c
#include <stdio.h>
#include "../code/adv.h"

#define SHOW(x) printf("%-18s -> %s\n", #x, TYPE_NAME(x))

static void print_int(int v) { printf("int %d\n", v); }
static void print_double(double v) { printf("double %g\n", v); }
static void print_str(char *v) { printf("string %s\n", v); }
static void print_other(const void *v) { (void)v; printf("unsupported\n"); }

/* Select a function, then call it: only the chosen call is type checked against its argument. */
#define print_g(x) _Generic((x), int: print_int, double: print_double, char *: print_str, default: print_other)(x)

int main(void)
{
    int i = 1;
    const int ci = 2;
    char name[8] = "abc";
    const char *cp = "abc";
    SHOW(i);
    SHOW(ci);
    SHOW('a');
    SHOW(1.5);
    SHOW(1.5f);
    SHOW(10000000000L);
    SHOW(name);
    SHOW("abc");
    SHOW(cp);
    SHOW(&i);
    SHOW(sizeof i);
    print_g(7);
    print_g(2.5);
    print_g(name);
    print_g(cp);
    printf("abs_g(-3) = %d, abs_g(-3.5) = %g, abs_g(-4000000000L) = %ld\n", abs_g(-3), abs_g(-3.5), abs_g(-4000000000L));
    return 0;
}
```

```text
i                  -> int
ci                 -> int
'a'                -> int
1.5                -> double
1.5f               -> float
10000000000L       -> long
name               -> char *
"abc"              -> char *
cp                 -> const char *
&i                 -> other
sizeof i           -> unsigned long
int 7
double 2.5
string abc
unsupported
abs_g(-3) = 3, abs_g(-3.5) = 3.5, abs_g(-4000000000L) = 4000000000
```

What to read in that output:

- `'a'` is an `int` in C (it is a `char` in C++). Integer promotions apply to the controlling expression, so `c + c` for two `char`s is an `int`.
- **An array decays**: both `name` (an array of 8 chars) and `"abc"` select `char *`. A string literal is not `const char *` for this purpose (the literal's type is `char[4]`), but a variable declared `const char *cp` is, and `char *` does not match it, which is why `print_g(cp)` printed `unsupported`. When you want both, list both: `char *: f, const char *: f`.
- **Qualifiers on the controlling expression are dropped** in GCC 13.3 (the lvalue conversion): `ci`, a `const int`, selected `int`. Older compilers differed here, and C23 clarified it; do not rely on `const int:` being a separate branch (from memory; the measured fact is the GCC 13.3 result).
- `sizeof` has type `unsigned long` here (`size_t`), which is why `TYPE_NAME(sizeof i)` is `unsigned long`.

**The trap: every branch is type checked.** Only the chosen branch is *evaluated*, but **all branches are compiled**. The first draft of `print_g` put a `printf` with a different format in each branch, and the compiler warned on the unselected ones:

```c
#include <stdio.h>

#define print_g(x) _Generic((x), int: printf("int %d\n", (x)), double: printf("double %g\n", (x)), default: printf("other\n"))

int main(void)
{
    print_g(7);
    return 0;
}
```

```text
warning: format '%g' expects argument of type 'double', but argument 2 has type 'int' [-Wformat=]
```

For `print_g(7)` the `%g` branch is type checked against an `int` and the compiler warns, even though it is never run. The fix is the one `print_g` in the demo uses: **select a function with `_Generic` and call it afterwards**, `_Generic((x), int: print_int, double: print_double, default: print_other)(x)`, so that only the selected function is ever called with `x`. The same shape is used for `abs_g`, which picks `abs`, `labs`, `llabs`, `fabsf` or `fabs` from the argument type, and for `<tgmath.h>` (the standard type-generic math header). `_Generic` does not give you overloading by argument count, and an unsupported type with no `default` is a compile error, which is usually what you want.

**`_Static_assert(expr, "message")`** (C11; the message is optional in C23) fails the compile when `expr` is false: use it for `sizeof` and `offsetof` expectations of a layout that is shared with hardware or a protocol, for `sizeof(int) == 4` style assumptions, and for table lengths. **`_Alignas(n)`** and **`_Alignof(T)`** (C11; spelled `alignas` and `alignof` with `<stdalign.h>`) control alignment: `_Alignas(64)` places a variable or a member on a cache line (section 7 shows why), and `aligned_alloc(64, size)` returns heap memory with that alignment (the size must be a multiple of the alignment in C11).

**Safe macros.** Section 3 of C 06 explained why macros need care. The kernel versions of the three common ones use two GNU extensions, statement expressions and `__typeof__`:

```c
#define BUILD_BUG_ON_ZERO(e) ((int)sizeof(struct { int : (-!!(e)); }))
#define MUST_BE_ARRAY(a) BUILD_BUG_ON_ZERO(__builtin_types_compatible_p(__typeof__(a), __typeof__(&(a)[0])))
/* The element count of a real array; applying it to a pointer is a compile error. */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]) + MUST_BE_ARRAY(a))

/* Evaluates each argument once and warns when the two types differ. */
#define min(a, b) ({ __typeof__(a) a_ = (a); __typeof__(b) b_ = (b); (void)(&a_ == &b_); a_ < b_ ? a_ : b_; })
#define max(a, b) ({ __typeof__(a) a_ = (a); __typeof__(b) b_ = (b); (void)(&a_ == &b_); a_ > b_ ? a_ : b_; })

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
```

- **`ARRAY_SIZE(a)`** is `sizeof(a) / sizeof((a)[0])` plus a term that is zero for a real array and a **compile error for a pointer**. A function parameter `int a[]` is a pointer, and the unchecked division silently gives the wrong count:

```c
#include "../code/adv.h"

int pick(int a, long b)
{
    return (int)min(a, b);
}

int count(int *p)
{
    return (int)ARRAY_SIZE(p);
}
```

```text
warning: comparison of distinct pointer types lacks a cast
warning: division 'sizeof (int *) / sizeof (int)' does not compute the number of array elements [-Wsizeof-pointer-div]
error: negative width in bit-field '<anonymous>'
```

  The pointer case produced a warning from `-Wsizeof-pointer-div` and the error `negative width in bit-field`, which is the trick: `BUILD_BUG_ON_ZERO(e)` is `sizeof(struct { int : -!!(e); })`, a bit-field of negative width when `e` is true, and zero otherwise. The same expression is the old form of `BUILD_BUG_ON`, and `_Static_assert` is the modern replacement for statements.
- **`min` and `max`** copy each argument into a local of its own type (**evaluating it once**, so `min(i++, j)` is safe), and the comparison `&a_ == &b_` between pointers to the two types **warns when the types differ**, as the first line of the output shows for `min(a, b)` with an `int` and a `long`. The tests confirm the single evaluation (an argument with a side effect counter was evaluated exactly once) and the result against the operators on random integers, longs and doubles.
- **`likely` and `unlikely`** wrap `__builtin_expect`; section 9 measures them.

---

## 6. Variable length arrays

A **VLA** is an array whose size is a run time value, C99. It lives on the stack and costs one stack pointer adjustment:

```c
#include <stdio.h>

static void fill(int rows, int cols, int a[rows][cols])
{
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j)
            a[i][j] = i * cols + j;
}

static long trace_sum(int n, int (*a)[n])
{
    long s = 0;
    for (int i = 0; i < n; ++i)
        s += a[i][i];
    return s;
}

int main(void)
{
    int rows = 3, cols = 4;
    int m[rows][cols];                       /* a variable length array on the stack */
    fill(rows, cols, m);
    printf("sizeof(m) = %zu (evaluated at run time), m[2][3] = %d\n", sizeof m, m[2][3]);
    int sq[3][3];
    fill(3, 3, sq);
    printf("trace of the 3x3 matrix = %ld\n", trace_sum(3, sq));
    return 0;
}
```

```text
sizeof(m) = 48 (evaluated at run time), m[2][3] = 11
trace of the 3x3 matrix = 12
```

What it does well is **the typed multidimensional parameter**: `int a[rows][cols]` with `rows` and `cols` declared before it, or `int (*a)[n]` as in `trace_sum`, lets you write `a[i][j]` for a matrix of run time dimensions without computing `i * cols + j` by hand. The pointer to a VLA type is the part that works. It is checked by the compiler and costs nothing. `sizeof` on a VLA is evaluated at run time (48 bytes above).

What it risks:

- **Stack overflow on large or untrusted sizes**, with no failure return and no way to test for it: a VLA of attacker chosen size is a denial of service or worse, and the stack size is 8 MB on a default Linux thread and often far less in a kernel or an embedded task (C 08 section on stack exhaustion shows the crash). The Linux kernel removed VLAs entirely for this reason and builds with `-Wvla` (from memory).
- **Skipped guard pages**: a large VLA can jump past the guard page, a known exploit technique (from memory; `-fstack-clash-protection` addresses it).
- **No initialisation and no `= {0}`.**
- **Optional in C11 and later**: a compiler may define `__STDC_NO_VLA__` and not support them (MSVC does not; from memory).

`-Wvla` warns about each VLA, and `-Wvla-larger-than=N` bounds them. GCC's wording is odd, because the same warning serves the old standard modes:

```text
d9_vla.c:10:1: warning: ISO C90 forbids variable length array 'a' [-Wvla]
d9_vla.c:21:5: warning: ISO C90 forbids variable length array 'm' [-Wvla]
d9_vla.c:3:1: warning: ISO C90 forbids variable length array 'a' [-Wvla]
```

C23 makes the **variably modified types** (pointer to VLA, as in the parameters above) mandatory but keeps the VLA *objects* optional (from memory). The pattern that stays safe is to use the types for parameters and to allocate the storage with `malloc`, checked.

---

## 7. Atomics and threads

C11 added `<stdatomic.h>` (atomic types and operations), `<threads.h>` (threads, mutexes, condition variables), and a memory model; glibc 2.39 provides both headers, and the threads here use `thrd_create`. This section is the C language side of the topics in OS 06 to OS 08: the operating system gives you the scheduler and the futex, and the language gives you a way to say what is allowed to be reordered.

**A data race is undefined behaviour.** Two threads access the same object, at least one writes, and nothing orders them. The result is not "an occasional wrong number"; the compiler may assume it cannot happen. Four threads each incremented three counters one million times:

```c
#include <stdatomic.h>
#include <stdio.h>
#include <threads.h>

#define THREADS 4
#define PER 1000000

static int plain;
static atomic_int atomic_count;
static int locked;
static mtx_t lock;

static int worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < PER; ++i) {
        plain++;                                   /* a data race: undefined behaviour */
        atomic_fetch_add(&atomic_count, 1);
        mtx_lock(&lock);
        locked++;
        mtx_unlock(&lock);
    }
    return 0;
}

int main(void)
{
    thrd_t t[THREADS];
    mtx_init(&lock, mtx_plain);
    for (int i = 0; i < THREADS; ++i)
        thrd_create(&t[i], worker, NULL);
    for (int i = 0; i < THREADS; ++i)
        thrd_join(t[i], NULL);
    printf("expected %d\n", THREADS * PER);
    printf("plain int        %d\n", plain);
    printf("atomic_fetch_add %d\n", atomic_load(&atomic_count));
    printf("mutex            %d\n", locked);
    return 0;
}
```

```text
expected 4000000
plain int        3860562
atomic_fetch_add 4000000
mutex            4000000
```

The plain `int` lost about 100,000 updates out of four million (the number differs on every run; this is the one printed), and `atomic_fetch_add` and the mutex were exact. The plain increment is a load, an add and a store, and two threads interleave between them.

**Publishing data with release and acquire.** The more important and more subtle case is a flag that says "the data is ready". Here is the natural, wrong version, and what happens to it:

```c
#include <stdio.h>
#include <pthread.h>

static int data;
static int ready;                 /* a plain flag: no synchronisation */

static void *producer(void *arg)
{
    (void)arg;
    data = 42;
    ready = 1;
    return NULL;
}

int main(void)
{
    pthread_t t;
    pthread_create(&t, NULL, producer, NULL);
    while (!ready)
        ;
    printf("data = %d\n", data);
    pthread_join(t, NULL);
    return 0;
}
```

```text
-O0: data = 42
-O2: did not finish in 5 s
```

At `-O0` it works. At `-O2` **it never finishes**: the loop `while (!ready)` reads `ready` once and, because nothing in the loop can change it as far as a single threaded compiler can see, spins forever on the register copy. A tool that sees both threads, ThreadSanitizer, reports both races: the flag and the data it was meant to guard.

```text
WARNING: ThreadSanitizer: data race
  Write of size 4 at ADDR by thread T1:
    #0 producer pe_race.c:11
  Previous read of size 4 at ADDR by main thread:
    #0 main pe_race.c:19
SUMMARY: ThreadSanitizer: data race pe_race.c:11 in producer
WARNING: ThreadSanitizer: data race
  Read of size 4 at ADDR by main thread:
    #0 main pe_race.c:21
  Previous write of size 4 at ADDR by thread T1:
    #0 producer pe_race.c:10
SUMMARY: ThreadSanitizer: data race pe_race.c:21 in main
```

The repair uses `memory_order_release` on the store of the flag and `memory_order_acquire` on its load. A release store guarantees that every write before it is visible to a thread whose acquire load sees the stored value:

```c
#include <stdatomic.h>
#include <stdio.h>
#include <pthread.h>

static int data;
static atomic_int ready;

static void *producer(void *arg)
{
    (void)arg;
    data = 42;                                              /* an ordinary write ... */
    atomic_store_explicit(&ready, 1, memory_order_release); /* ... published by the release store */
    return NULL;
}

int main(void)
{
    pthread_t t;
    pthread_create(&t, NULL, producer, NULL);
    while (!atomic_load_explicit(&ready, memory_order_acquire))
        ;                                                   /* the acquire load pairs with it */
    printf("data = %d\n", data);
    pthread_join(t, NULL);
    return 0;
}
```

```text
fixed, -O2: data = 42
fixed, ThreadSanitizer: 0 warnings, output: data = 42
```

It works at `-O2`, and ThreadSanitizer reports no race. The memory orders in short: `memory_order_relaxed` (atomicity only, no ordering; fine for a statistics counter), `acquire` and `release` (pairwise ordering, the right tool for handing data from one thread to another), and `seq_cst` (the default for the plain `atomic_load` and `atomic_store`: one total order of all such operations, the safest and the most expensive on some architectures). On x86-64 the hardware orders all ordinary loads and stores strongly, so `release` and `acquire` compile to plain moves; on ARM64 and POWER they need barrier instructions (from memory). **A test on x86 cannot prove your ordering correct**, which is what the two escaped mutants below show.

**A lock free ring buffer.** One producer and one consumer, no locks. `head` is written only by the producer and `tail` only by the consumer, both only grow, and the slot is `index & (N - 1)`:

```c
/* A lock free single producer, single consumer ring buffer. head and tail only ever grow. */
#define SPSC_N 8u
typedef struct {
    _Atomic size_t head;     /* written by the producer */
    _Atomic size_t tail;     /* written by the consumer */
    int buf[SPSC_N];
} Spsc;

static int spsc_push(Spsc *q, int v)
{
    size_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    size_t t = atomic_load_explicit(&q->tail, memory_order_acquire);
    if (h - t == SPSC_N)
        return -1;                                                  /* full */
    q->buf[h & (SPSC_N - 1)] = v;
    atomic_store_explicit(&q->head, h + 1, memory_order_release);   /* publish the element */
    return 0;
}

static int spsc_pop(Spsc *q, int *out)
{
    size_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    size_t h = atomic_load_explicit(&q->head, memory_order_acquire);
    if (h == t)
        return -1;                                                  /* empty */
    *out = q->buf[t & (SPSC_N - 1)];
    atomic_store_explicit(&q->tail, t + 1, memory_order_release);   /* give the slot back */
    return 0;
}
```

The producer writes the element, then publishes it with a **release** store of `head`; the consumer's **acquire** load of `head` makes the element visible. The same pair runs in reverse for `tail`, which gives a slot back. Two tests ran: a single threaded model of 500,000 random pushes and pops against a plain queue of capacity 8 (checking full and empty), and a two thread run in which the producer pushes 1 to 1,000,000 and the consumer checks they arrive in order. Mutants that never reported full, did not advance the tail, did not mask the index, or never reported empty were all caught. **Two mutants escaped**: the `release` store replaced by `relaxed`, and the `acquire` load replaced by `relaxed`. Both are real bugs on ARM64 and invisible on x86. ThreadSanitizer sees the first one in the program below, which still prints the right sum:

```c
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include "../code/adv.h"

static Spsc q;
#define N 20000

static void *producer(void *arg)
{
    (void)arg;
    for (int i = 1; i <= N; ++i)
        while (spsc_push(&q, i) != 0)
            sched_yield();
    return NULL;
}

int main(void)
{
    pthread_t th;
    pthread_create(&th, NULL, producer, NULL);
    long sum = 0;
    int got = 0, v;
    while (got < N) {
        if (spsc_pop(&q, &v) == 0) {
            sum += v;
            got++;
        } else {
            sched_yield();
        }
    }
    pthread_join(th, NULL);
    printf("received %d items, sum %ld (expected %ld)\n", got, sum, (long)N * (N + 1) / 2);
    return 0;
}
```

```text
received 20000 items, sum 200010000 (expected 200010000)
```

The unmodified header is clean under ThreadSanitizer, and with the `release` store weakened to `relaxed` it reports:

```text
WARNING: ThreadSanitizer: data race
  Read of size 4 at ADDR by main thread:
    #0 spsc_pop adv.h:294
  Previous write of size 4 at ADDR by thread T1:
    #0 spsc_push adv.h:283
SUMMARY: ThreadSanitizer: data race adv.h:294 in spsc_pop
received 20000 items, sum 200010000
```

The read of the element in `spsc_pop` races with its write in `spsc_push`: without the release and acquire pair there is no happens-before edge, and the tool reports it although the program computed the right answer on x86. This is the practical way to check ordering: **run the threaded tests under `-fsanitize=thread`**. (ThreadSanitizer failed on the C11 `thrd_create` threads with GCC 13.3 in a first attempt, with a crash inside the tool; the programs above that use it create their threads with `pthread_create`. I did not look into why.)

**A spinlock** with `atomic_flag`: `test_and_set` with `acquire` to take it and `clear` with `release` to drop it. The release and acquire pair is what makes the critical section's writes visible to the next owner:

```c
#include <stdatomic.h>
#include <stdio.h>
#include <threads.h>

typedef struct { atomic_flag f; } Spin;

static void spin_lock(Spin *s)
{
    while (atomic_flag_test_and_set_explicit(&s->f, memory_order_acquire))
        ;                                              /* spin: someone else holds it */
}
static void spin_unlock(Spin *s) { atomic_flag_clear_explicit(&s->f, memory_order_release); }

static Spin lock = {ATOMIC_FLAG_INIT};
static long counter;                                   /* plain: protected by the lock */

static int worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < 500000; ++i) {
        spin_lock(&lock);
        counter++;
        spin_unlock(&lock);
    }
    return 0;
}

int main(void)
{
    thrd_t t[4];
    for (int i = 0; i < 4; ++i)
        thrd_create(&t[i], worker, NULL);
    for (int i = 0; i < 4; ++i)
        thrd_join(t[i], NULL);
    printf("4 threads x 500000 increments under the spinlock: %ld (expected 2000000)\n", counter);
    return 0;
}
```

```text
4 threads x 500000 increments under the spinlock: 2000000 (expected 2000000)
```

The plain `long counter` is protected by the lock, so there is no race. A spinlock burns a CPU while it waits and is only right for very short critical sections that cannot sleep (the kernel's `spin_lock` with interrupts disabled), and never when the holder can be descheduled.

**What synchronisation costs.** I incremented a counter 20,000,000 times per thread in five ways:

```text
T3 counters, 20000000 increments per thread: plain 20000000, atomic one thread 20000000, mutex one thread 20000000, atomic two threads 40000000, mutex two threads 40000000
```

| Counter | Time (ms) | Per increment |
|---|---|---|
| Plain `volatile`, one thread | 49 / 47 | about 2.4 ns |
| `atomic_fetch_add`, one thread | 127 / 122 | about 6 ns |
| Mutex, one thread | 182 / 170 | about 9 ns |
| `atomic_fetch_add`, two threads, one counter | 630 / 588 | about 15 ns (40 million increments) |
| Mutex, two threads, one counter | 3085 / 3183 | about 77 ns (40 million increments) |

An uncontended atomic costs two to three times a plain increment (a locked instruction), an uncontended mutex a little more, and **contention is what hurts**: with two threads on the same counter the atomic version takes about 2.5 times its single thread rate per operation, and the mutex about 9 times, because a contended `mtx_lock` goes through the kernel to sleep and wake (futex, OS 06). The conclusion is not "never use mutexes": it is that **a shared counter updated on every operation by many threads is a design problem**, and the fixes are per thread counters summed at the end, batching, or sharding.

**False sharing.** Two threads that each touch their *own* variable still fight over the cache line if the variables share one. I gave each of two threads its own counter, adjacent in memory or aligned to separate 64 byte lines with `_Alignas(64)`:

```text
T2 two threads, each increments its own counter: volatile plain counters (100000000 each) adjacent 100000000 100000000 16 and padded 100000000 100000000 128; atomic counters (20000000 each) adjacent 20000000 20000000 16 and padded 20000000 20000000 128
```

| Increments | Adjacent counters (ms) | Separate cache lines (ms) |
|---|---|---|
| Atomic, 20,000,000 per thread | 600 / 530 | 130 / 124 |
| Plain `volatile`, 100,000,000 per thread | 250 / 251 | 248 / 245 |

With atomic increments, putting the counters on separate lines made the program **about four to five times faster**. With plain `volatile` increments there was **no difference**. I do not know why: a plain store goes through the store buffer and does not wait for the line to move, and each loop is limited by store to load forwarding (about 2.4 ns an iteration in both cases). I did not look at the hardware counters, and the container does not say how its two vCPUs map to physical cores, so treat the null result as specific to this machine. The atomic result is the one to remember: **per thread data that is updated often must not share a cache line**, which is why the kernel has `____cacheline_aligned_in_smp` and per CPU variables (from memory).

---

## 8. Signals and async-signal-safety

A signal handler runs **between any two instructions** of the interrupted thread, possibly inside `malloc` or `printf`. If the handler calls the same non-reentrant function, it deadlocks or corrupts state. The standard allows a handler to do very little: assign to a `volatile sig_atomic_t`, call `_Exit`, and a short list of other functions; POSIX adds a longer list of **async-signal-safe** functions (`write`, `_exit`, `kill`, `sigaction` and others; `man 7 signal-safety` lists them). `printf`, `malloc`, `free` and `exit` are **not** on it.

```c
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t got;

static void handler(int sig)
{
    got = sig;                              /* the only thing a handler should do: set a flag */
    static const char msg[] = "  handler: write() is async-signal-safe\n";
    ssize_t r = write(1, msg, sizeof msg - 1);
    (void)r;
}

int main(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);
    raise(SIGUSR1);
    printf("main saw signal %d (SIGUSR1 is %d)\n", (int)got, SIGUSR1);
    return 0;
}
```

```text
  handler: write() is async-signal-safe
main saw signal 10 (SIGUSR1 is 10)
```

The handler sets a flag and uses `write`, which is safe; `printf` in a handler would work in a demo and fail once in a million runs, which is the worst kind of bug. Rules: **keep the handler tiny, set a flag, and do the work in the main loop**; use `sigaction`, not `signal` (its semantics vary); block the signal while you hold a lock the handler might need; **save and restore `errno` in a handler** that makes any call, because the interrupted code may be about to read it; and for waking a `poll` loop from a handler, write a byte to a pipe (the self-pipe trick, or `signalfd`, from memory).

---

## 9. GCC and Clang extensions the kernel relies on

The Linux kernel is written in GNU C, not ISO C (it builds with `-std=gnu11`, from memory, and the two compilers GCC and Clang agree on the extensions below). Every one was met above or is short enough to show.

| Extension | What it does | Where |
|---|---|---|
| `__attribute__((format(printf, f, a)))` | Checks printf-style calls | Section 1 |
| `__attribute__((cleanup(f)))` | Calls `f` at scope exit | Section 3 |
| Statement expressions `({ ... })`, `__typeof__` | Type-safe, single evaluation macros | Section 5 |
| `__attribute__((packed))`, `aligned(n)` | Layout control | C 09, section 7 |
| `__attribute__((section("name")))` | Places a symbol in a named section | below |
| `__attribute__((noreturn))`, `always_inline`, `noinline`, `unused`, `used`, `weak` | Control calls and linking | below |
| `__builtin_expect`, `__builtin_unreachable` | Optimiser hints | below |
| `__builtin_clz`, `ctz`, `popcount`, `add_overflow` | CPU instructions | C 07, C 09 |

**`section` and the linker-defined symbols** are how the kernel builds tables with no central list: each driver puts a pointer to its init function in a section, and the linker gathers them. For a section whose name is a valid C identifier, the linker also defines `__start_NAME` and `__stop_NAME`:

```c
#include <stdio.h>

typedef void (*init_fn)(void);

/* Every function marked INITCALL puts a pointer into the section "initcalls". */
#define INITCALL(fn) static init_fn fn##_ptr __attribute__((used, section("initcalls"))) = fn

static void init_clock(void) { printf("  init: clock\n"); }
static void init_uart(void) { printf("  init: uart\n"); }
static void init_net(void) { printf("  init: net\n"); }
INITCALL(init_clock);
INITCALL(init_uart);
INITCALL(init_net);

/* The linker defines these two symbols for any section whose name is a C identifier. */
extern init_fn __start_initcalls[];
extern init_fn __stop_initcalls[];

int main(void)
{
    printf("%td functions registered without a central list\n", __stop_initcalls - __start_initcalls);
    for (init_fn *p = __start_initcalls; p < __stop_initcalls; ++p)
        (*p)();
    return 0;
}
```

```text
3 functions registered without a central list
  init: net
  init: uart
  init: clock
```

Three functions registered themselves in three separate places, and the loop found them with no list to maintain. **The order is not the source order**: the linker placed them in the reverse order here, so the kernel's `initcall` levels use separate sections (`.initcall1.init`, `.initcall2.init` and so on) sorted by the linker script (from memory) when order matters. The `used` attribute stops the compiler from dropping the otherwise unreferenced variable. This is also how `module_init`, `EXPORT_SYMBOL` tables and `__setup` command line options work (from memory).

**`noreturn`** lets the compiler drop the code after a call and avoid "missing return" warnings (`abort`, `exit`, kernel `panic`). **`always_inline`** forces inlining; **`noinline`** prevents it (used in the experiments to stop the optimiser seeing through a call). **`weak`** lets a symbol be overridden by a strong definition elsewhere. **`__builtin_unreachable()`** tells the optimiser a point cannot be reached: if it is reached, behaviour is undefined, so it is a promise and not a check. (C23 standardises it as `unreachable()`.)

**Does `likely` help?** I ran a loop over 16,777,216 values, 40 passes, with an error branch taken one time in a thousand, built with no hint, with `unlikely` (the correct hint), and with `likely` (the **wrong** hint):

```text
T1 40 passes over 16777216 values, an error branch taken 1 time in 1000: checksums no hint 1416352439783338680, unlikely hint 1416352439783338680, likely hint 1416352439783338680
```

| Hint on the error branch | Time (ms) |
|---|---|
| None | 618 / 618 |
| `unlikely` (correct) | 491 / 563 |
| `likely` (wrong) | 535 / 555 |

All three computed the same checksum. Both hints were **10 to 20 percent faster than no hint**, and the correct hint was not clearly better than the wrong one: the run to run differences are as large as the gap between them. I did not read the assembly, so I cannot say why a hint of either kind helped; code layout is the likely reason (the hint moves the call to `handle_error` out of line), but that is a hypothesis. The honest conclusion is that on a modern CPU the hardware branch predictor handles a one in a thousand branch regardless of the hint, the hint changes layout, and the effect is small and not guaranteed. Use `likely` and `unlikely` for error paths in hot code, as the kernel does, and **do not expect a large gain without measuring**.

---

## 10. Floating point

Floating point is a binary approximation, and C inherits all of it. Five facts, each shown by a run:

```c
#include <math.h>
#include <stdio.h>

int main(void)
{
    double a = 0.1, b = 0.2, c = 0.3;
    printf("0.1 + 0.2 == 0.3 ? %s;  0.1 + 0.2 = %.17g, 0.3 = %.17g\n", a + b == c ? "yes" : "no", a + b, c);
    printf("fabs(a + b - c) < 1e-12 ? %s\n", fabs(a + b - c) < 1e-12 ? "yes" : "no");

    volatile double zero = 0.0;
    double nan_ = zero / zero;
    printf("NaN: nan == nan %d, nan != nan %d, nan < 1 %d, nan > 1 %d, isnan %d\n", nan_ == nan_, nan_ != nan_, nan_ < 1, nan_ > 1, isnan(nan_));

    float fsum = 0.0f;
    double dsum = 0.0;
    for (int i = 0; i < 10000000; ++i) {
        fsum += 0.1f;
        dsum += 0.1;
    }
    printf("adding 0.1 ten million times (the exact sum is 1000000): float %.1f, double %.4f\n", fsum, dsum);

    double big = 1e10;
    volatile double v = big;
    int n = (int)v;
    printf("(int)1e10 gives %d here, but the C standard calls it undefined\n", n);
    return 0;
}
```

```text
0.1 + 0.2 == 0.3 ? no;  0.1 + 0.2 = 0.30000000000000004, 0.3 = 0.29999999999999999
fabs(a + b - c) < 1e-12 ? yes
NaN: nan == nan 0, nan != nan 1, nan < 1 0, nan > 1 0, isnan 1
adding 0.1 ten million times (the exact sum is 1000000): float 1087937.0, double 999999.9998
(int)1e10 gives -2147483648 here, but the C standard calls it undefined
```

- **`0.1 + 0.2 == 0.3` is false.** Neither 0.1 nor 0.2 nor 0.3 is exactly representable in binary; the sum is `0.30000000000000004`. Compare with a tolerance (`fabs(a - b) < eps`), chosen from the scale of the values, or compare integers (money in cents).
- **NaN is not equal to anything, including itself.** All comparisons with a NaN are false except `!=`, so `x != x` is a NaN test, and `isnan(x)` is the portable one. A NaN that gets into a sort comparator or a hash key breaks the ordering the algorithm needs.
- **Accumulated error grows.** Adding `0.1f` ten million times should give 1,000,000. The `float` result is **1,087,937**, 8.8 percent too high, because once the running sum is large the small addend is rounded to a coarser grid; the `double` is `999999.9998`. Remedies: a wider accumulator, pairwise summation, or Kahan summation.
- **Converting a float to an integer that cannot hold it is undefined.** `(int)1e10` gave `INT_MIN` here (the x86 instruction `cvttss2si` returns that value for an out-of-range input), but the standard gives no promise, and the compiler may assume it cannot happen. UBSan reports it:

```text
d10_float.c:24:5: runtime error: 1e+10 is outside the range of representable values of type 'int'
```

- **`-ffast-math` changes the meaning of the program.** It allows the compiler to assume no NaN or infinity, to treat addition as associative, and more. A NaN test is then **removed**:

```text
gcc -O2:              isnan(0.0/0.0) = 1,  x != x = 1
gcc -O2 -ffast-math:  isnan(0.0/0.0) = 0,  x != x = 0
```

  The identical source printed `isnan = 1` at `-O2` and `isnan = 0` with `-ffast-math`, because the flag told the compiler that NaN cannot occur, so `x != x` is false. The benefit is speed: associativity lets the compiler vectorise a float sum. I summed 1,048,576 floats 200 times:

```text
T4 float sum of 1048576 values, 200 passes: strict 1.677722e+07 1.046576e+08, with -ffast-math 6.710886e+07 1.046576e+08
```

| Build | Time (ms) | Result |
|---|---|---|
| Strict (`-O2`) | 154 / 146 | 1.677722e+07 |
| `-ffast-math` | 36 / 37 | 6.710886e+07 |

  The fast version is about four times faster, and **both results are wrong**: the correct sum, from a `double` accumulator in the same program, is 1.046576e+08. The strict float sum stuck at exactly 2^24 = 16,777,216, the point where a float can no longer represent the sum plus a number below 1, so every later addition does nothing. The fast result is exactly 4 x 2^24: my explanation is that the vectorised loop keeps four partial sums (one per SSE lane) and each one stuck at 2^24, but I did not read the assembly, so that is a hypothesis. Both errors come from the same cause, a float accumulator that is too narrow, and it is a reminder that "faster and different" is not the same as "faster and right". Do not use `-ffast-math` for code that depends on exact semantics (financial, anything with NaN checks, numerical code with compensated summation, which the flag may optimise away); enable the single flags you can justify (`-fno-math-errno`, `-fno-trapping-math`) instead.

---

## 11. What C23 adds

C23 (ISO/IEC 9899:2024) is the first new C standard since C17 with real language changes. GCC 13.3 accepts it as `-std=c2x`. I compiled one small file per feature and recorded the result:

```text
nullptr: compiles
constexpr object: compiles
typeof: compiles
auto: compiles
bool, true, false keywords: compiles
[[nodiscard]]: compiles with a diagnostic, warning: ignoring return value of 'f', declared with attribute 'nodiscard' [-Wunused-result]
[[maybe_unused]]: compiles
binary literal and digit separators: compiles
static_assert without a message: compiles
_BitInt: REJECTED, error: expected declaration specifiers or '...' before numeric constant
enum with a fixed underlying type: compiles
empty initialiser: compiles
unreachable() from stddef.h: compiles
typeof_unqual: compiles
#elifdef: compiles
#embed: REJECTED, error: invalid preprocessing directive #embed
```

Reading the list: **`nullptr`** (a typed null pointer constant, ends the `0` against `NULL` ambiguity in variadic calls), **`constexpr`** objects, **`typeof` and `typeof_unqual`** (the extension of section 5, now standard), **`auto`** type inference for objects, **`bool`, `true` and `false` as keywords**, **attributes** in double brackets (`[[nodiscard]]` warns when a result is ignored, which is how `warn_unused_result` of section 2 is spelled in the standard; `[[maybe_unused]]`, `[[deprecated]]`), **binary literals and digit separators** (`0b1010`, `1'000'000`), **`static_assert` without a message**, **enums with a fixed underlying type**, **`= {}` as the empty initialiser**, **`unreachable()`**, and **`#elifdef`**. Two were rejected by this compiler: **`_BitInt(N)`** (a bit-precise integer type) and **`#embed`** (include a file's bytes as an initialiser list, the end of `xxd -i`). GCC's C status page (checked) lists `_BitInt` from GCC 14 and `#embed` from GCC 15, so check your compiler version before relying on either. C23 also made two's complement the only representation of signed integers (signed overflow is still undefined), removed K&R function definitions, and added `<stdckdint.h>` with `ckd_add` and friends for overflow checking, standardising the `__builtin_add_overflow` of C 09 (from memory; I did not test `<stdckdint.h>` here).

---

## What it costs

| Feature | Cost | Evidence |
|---|---|---|
| Variadic call | No type check unless `format` is set; wrong types are undefined behaviour | `pb_avg`: garbage `4.52606e-316` |
| `va_copy` two pass formatting | One extra formatting pass | Section 1 |
| `goto` cleanup, `cleanup` attribute | No run time cost beyond the code | Section 3 |
| `setjmp` and `longjmp` | Saves registers per `setjmp`; skips all cleanup | `d2leak`, `d2d` |
| `_Generic` | None at run time | Section 5 |
| VLA | One stack adjustment; stack overflow risk | Section 6 |
| Atomic increment, no contention | About 2.5x a plain increment | T3 |
| Mutex, contended | About 30x a plain increment | T3 |
| False sharing, atomic counters | About 4 to 5x slower | T2 |
| `likely` and `unlikely` | None; gain measured at 10 to 20 percent, with either hint | T1 |
| `-ffast-math` | Different results, NaN tests removed; sum 4x faster | T4, `d10b` |

---

## Common mistakes

1. `va_arg(ap, float)`, `va_arg(ap, char)` or `va_arg(ap, short)`: the types are promoted. Use `double` and `int`.
2. Passing the wrong type to a variadic function (an `int` where it reads a `double`): garbage, not a crash.
3. A printf-like function without `__attribute__((format))`: the compiler cannot check its calls.
4. Using a `va_list` twice without `va_copy`.
5. Reading `errno` after a call that did not fail, or after another call: it is only valid right after a failure, and `errno` must be zeroed first for `strtol`.
6. `atoi` on untrusted text: no error reporting. Use a strict parser.
7. Misordered labels in a `goto` ladder: a leak or a double free. Test every failure point with a failing allocator.
8. Reading a local variable after a `longjmp` that changed it without `volatile`: the value depends on the optimisation level.
9. Expecting `longjmp` to free memory or run the `cleanup` attribute: it does neither.
10. Returning or breaking out of a `TRY` body, leaving a stale handler on the stack.
11. `ARRAY_SIZE` on a function parameter: it is a pointer. Use the checked macro.
12. Putting `printf` calls in the branches of a `_Generic`: every branch is type checked. Select a function and call it afterwards.
13. A VLA sized from untrusted input.
14. A plain `int` flag shared between threads: the compiler may hoist the load out of the loop, and the program hangs at `-O2`.
15. Using `memory_order_relaxed` where data is published, or testing the ordering only on x86.
16. Many threads updating one shared counter, or two per-thread counters in one cache line.
17. Calling `printf`, `malloc` or `exit` in a signal handler.
18. Using `-ffast-math` and keeping NaN checks.
19. Comparing floating point values with `==`, or converting an out-of-range float to an integer.

---

## Questions out loud

1. Why does `va_arg(ap, float)` not work? What are the default promotions?
2. How does a variadic function know how many arguments it received? Give three conventions and the macro trick that removes the count.
3. Why does `xasprintf` need `va_copy`? What would happen without it?
4. Compare returning `-1` and setting `errno` with returning a negative error code. When is each used? What are the rules for `errno`?
5. Write the `strtol` checks for a complete, range limited parse. What are the three ways it can fail?
6. Write a function that acquires three resources with `goto` cleanup. What is the order of the labels, and why?
7. What does the `cleanup` attribute do? What does it not do on a `longjmp`?
8. What does `setjmp` return, and how many times? What is the rule for local variables, and why does it depend on the optimisation level?
9. When is `longjmp` the right tool, and what must you do about memory allocated between the two calls?
10. What does `_Generic` evaluate? Why did a `printf` in every branch give warnings? How do you fix it?
11. How does `ARRAY_SIZE` reject a pointer? Why is `min(a, b)` written with statement expressions?
12. Why can a VLA be dangerous? Where is the pointer to a VLA type still useful?
13. Why is a data race undefined behaviour and not just a stale value? Why did the flag loop hang at `-O2`?
14. Explain release and acquire with the ring buffer. What does a relaxed load of the head lose? Why will the tests not fail on x86?
15. What is false sharing? How would you fix it, and how would you find it?
16. Why must a signal handler be tiny? Name two functions that are safe and two that are not.
17. What does `-ffast-math` allow? Give an example of code it breaks.
18. Name three C23 features and say which one your compiler lacks.

---

## Problems

Five problems, in order of difficulty. **E (entry level)**, **M (medium)**, **H (hard)**.

**A (E).** Predict the type name that `TYPE_NAME` gives for each expression below, then compile and compare. Pay attention to promotions.

```c
#include <stdio.h>
#include "../code/adv.h"

#define SHOW(x) printf("%-22s -> %s\n", #x, TYPE_NAME(x))

int main(void)
{
    char c = 'x';
    short s = 1;
    unsigned char uc = 200;
    float f = 1.0f;
    long l = 5;
    SHOW(c);
    SHOW(c + c);
    SHOW(s * s);
    SHOW(uc);
    SHOW(f * 2);
    SHOW(f * 2.0f);
    SHOW(l + 1u);
    SHOW(1 / 2.0);
    SHOW('a' + 1L);
    SHOW(sizeof c);
    SHOW(&c);
    return 0;
}
```

**B (E).** The program below averages its arguments. One call is correct and one is not. Say which, explain why, run it, and write a safe version.

```c
#include <stdarg.h>
#include <stdio.h>

static double avg(int n, ...)
{
    va_list ap;
    va_start(ap, n);
    double total = 0;
    for (int i = 0; i < n; ++i)
        total += va_arg(ap, double);
    va_end(ap);
    return total / n;
}

int main(void)
{
    printf("avg(3, 1, 2, 3)       = %g\n", avg(3, 1, 2, 3));
    printf("avg(3, 1.0, 2.0, 3.0) = %g\n", avg(3, 1.0, 2.0, 3.0));
    return 0;
}
```

**C (M).** Write a `DEFER(function, argument)` macro that runs a function when the enclosing scope exits, however it exits, with several defers running in reverse order, using the `cleanup` attribute. Show it on a function with an early return.

**D (M).** Write a spinlock with `atomic_flag`, with the right memory orders, and show that four threads incrementing a plain counter under it get an exact count.

**E (H).** The program below publishes a value to another thread with a plain flag. Run it at `-O0` and at `-O2`, run it under ThreadSanitizer, explain what each shows, and fix it with the weakest memory orders that are still correct.

```c
#include <stdio.h>
#include <pthread.h>

static int data;
static int ready;                 /* a plain flag: no synchronisation */

static void *producer(void *arg)
{
    (void)arg;
    data = 42;
    ready = 1;
    return NULL;
}

int main(void)
{
    pthread_t t;
    pthread_create(&t, NULL, producer, NULL);
    while (!ready)
        ;
    printf("data = %d\n", data);
    pthread_join(t, NULL);
    return 0;
}
```

**Where to solve them.** All five need only GCC or Clang on Linux. Use `-std=gnu11 -Wall -Wextra`; for E add `-fsanitize=thread -g -O0` (use `pthread_create` threads, as the example does) and `-lpthread`. For C use the `cleanup` attribute from section 3. For D use `<stdatomic.h>` and either `<threads.h>` or `<pthread.h>`. For more practice, read the kernel's `include/linux/compiler.h` and `include/linux/container_of.h`, or the C11 chapter of the GCC manual on atomics (`__atomic` builtins), and compare each macro with the ones above.

**How to approach them.** A: apply the integer promotions (anything smaller than `int` becomes `int` in arithmetic) and the usual arithmetic conversions (the wider or the unsigned type wins). B: the `...` arguments are converted to `int` or `double` by the caller and the callee must ask for the type that was passed; make the macro count and convert the arguments. C: the macro declares a hidden local variable whose cleanup function calls your function; use `__LINE__` with a two level concatenation macro so that two defers in one scope get different names. D: `test_and_set` with `acquire` to take the lock, `clear` with `release` to drop it; the counter is a plain variable protected by it. E: first decide what the compiler is allowed to assume about a plain `int` in a loop, then use `atomic_store_explicit` with `release` and `atomic_load_explicit` with `acquire`, and check with ThreadSanitizer.

---

## Answers

**A.** The program printed:

```text
c                      -> char
c + c                  -> int
s * s                  -> int
uc                     -> unsigned char
f * 2                  -> float
f * 2.0f               -> float
l + 1u                 -> long
1 / 2.0                -> double
'a' + 1L               -> long
sizeof c               -> unsigned long
&c                     -> char *
```

`c` is a `char`, but `c + c` is an `int`: the operands are promoted before the addition, and the same for `s * s` and for `uc` in arithmetic. `f * 2` is a `float` (the `int` converts to `float`), and `1 / 2.0` is a `double`. `l + 1u` is `long`: on this platform `long` (64 bits) can represent every `unsigned int`, so the unsigned operand converts to `long`. `'a' + 1L` is `long`. `sizeof` yields `size_t`, here `unsigned long`. `&c` is `char *`.

**B.** The first call is wrong and the second is correct. `avg(3, 1, 2, 3)` passes `int`s, but `va_arg(ap, double)` fetches doubles: undefined behaviour, and it printed `4.52606e-316`. The second call passed doubles. The safe version counts and converts the arguments in a macro:

```c
#include <stdarg.h>
#include <stdio.h>

/* The argument types are part of the contract: this one takes doubles, and the macro makes sure it gets them. */
static double avg_arr(const double *v, size_t n)
{
    double total = 0;
    for (size_t i = 0; i < n; ++i)
        total += v[i];
    return n ? total / (double)n : 0.0;
}
#define AVG(...) avg_arr((double[]){__VA_ARGS__}, sizeof((double[]){__VA_ARGS__}) / sizeof(double))

int main(void)
{
    printf("AVG(1, 2, 3)       = %g\n", AVG(1, 2, 3));
    printf("AVG(1.0, 2.5, 3.0) = %g\n", AVG(1.0, 2.5, 3.0));
    return 0;
}
```

```text
AVG(1, 2, 3)       = 2
AVG(1.0, 2.5, 3.0) = 2.16667
```

With the compound literal `(double[]){...}`, the integers `1, 2, 3` are converted to `double` by the compiler (an ordinary initialisation) and the count cannot go wrong.

**C.** The cleanup function receives a pointer to the hidden variable, which holds the function and its argument. Two defers in one scope run in reverse order of declaration, and the early `return` runs them too:

```c
#include <stdio.h>

/* defer: run a statement when the enclosing scope ends, however it ends. Several defers run last in, first out. */
typedef struct { void (*fn)(void *); void *arg; } Defer;
static void run_defer(Defer *d) { d->fn(d->arg); }
#define CAT_(a, b) a##b
#define CAT(a, b) CAT_(a, b)
#define DEFER(f, a) __attribute__((cleanup(run_defer))) Defer CAT(defer_, __LINE__) = {(void (*)(void *))(f), (a)}

static void say(const char *s) { printf("  cleanup: %s\n", s); }

static int work(int fail)
{
    printf("work(%d) begins\n", fail);
    DEFER(say, "first acquired, released last");
    DEFER(say, "second acquired, released first");
    if (fail) {
        printf("  early return\n");
        return -1;
    }
    printf("  normal end\n");
    return 0;
}

int main(void)
{
    work(0);
    work(1);
    return 0;
}
```

```text
work(0) begins
  normal end
  cleanup: second acquired, released first
  cleanup: first acquired, released last
work(1) begins
  early return
  cleanup: second acquired, released first
  cleanup: first acquired, released last
```

The `DEFER` name uses `__LINE__` through `CAT` and `CAT_` (the two levels make the macro expand `__LINE__` before pasting) so two defers do not collide. It does not run on `longjmp`, as section 4 showed.

**D.** `test_and_set` with `acquire` and `clear` with `release` give the lock the ordering that makes the plain counter safe:

```c
#include <stdatomic.h>
#include <stdio.h>
#include <threads.h>

typedef struct { atomic_flag f; } Spin;

static void spin_lock(Spin *s)
{
    while (atomic_flag_test_and_set_explicit(&s->f, memory_order_acquire))
        ;                                              /* spin: someone else holds it */
}
static void spin_unlock(Spin *s) { atomic_flag_clear_explicit(&s->f, memory_order_release); }

static Spin lock = {ATOMIC_FLAG_INIT};
static long counter;                                   /* plain: protected by the lock */

static int worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < 500000; ++i) {
        spin_lock(&lock);
        counter++;
        spin_unlock(&lock);
    }
    return 0;
}

int main(void)
{
    thrd_t t[4];
    for (int i = 0; i < 4; ++i)
        thrd_create(&t[i], worker, NULL);
    for (int i = 0; i < 4; ++i)
        thrd_join(t[i], NULL);
    printf("4 threads x 500000 increments under the spinlock: %ld (expected 2000000)\n", counter);
    return 0;
}
```

```text
4 threads x 500000 increments under the spinlock: 2000000 (expected 2000000)
```

**E.** At `-O0` it printed `data = 42` by luck: the loop reloads the flag from memory. At `-O2` it **did not finish in 5 seconds**: the compiler is entitled to assume nobody else changes `ready`, loads it once, and spins on the value. ThreadSanitizer reports two data races, on `ready` and on `data`, because the flag does not order the write of `data` before the read. The repair publishes `data` with a release store and reads the flag with an acquire load, the weakest pair that is still correct; `relaxed` would fix the hang but not the ordering of `data`. The fixed program ran correctly at `-O2` and was clean under ThreadSanitizer:

```c
#include <stdatomic.h>
#include <stdio.h>
#include <pthread.h>

static int data;
static atomic_int ready;

static void *producer(void *arg)
{
    (void)arg;
    data = 42;                                              /* an ordinary write ... */
    atomic_store_explicit(&ready, 1, memory_order_release); /* ... published by the release store */
    return NULL;
}

int main(void)
{
    pthread_t t;
    pthread_create(&t, NULL, producer, NULL);
    while (!atomic_load_explicit(&ready, memory_order_acquire))
        ;                                                   /* the acquire load pairs with it */
    printf("data = %d\n", data);
    pthread_join(t, NULL);
    return 0;
}
```

```text
fixed, -O2: data = 42
fixed, ThreadSanitizer: 0 warnings, output: data = 42
```

---

## Cheat sheet

- Variadics: `va_start`, `va_arg(ap, int or double or pointer)`, `va_end`, `va_copy` for a second pass. Arguments are promoted. Put `__attribute__((format(printf, f, a)))` on printf-like functions. Prefer a macro with a compound literal to a count argument.
- Errors: pick one convention per layer. `errno = 0` before `strtol`, check no digits, trailing text and `ERANGE`, read `errno` only after a failure. Mark results `warn_unused_result` or `[[nodiscard]]`.
- Cleanup: nested `if` for one or two resources, `goto` ladder with labels in reverse order for many, the `cleanup` attribute with `STEAL` for scope based release. Test every failure point with a counting, failing allocator.
- `setjmp` returns twice. Variables changed after `setjmp` and read after `longjmp` must be `volatile`. `longjmp` runs no cleanup and skips the `cleanup` attribute. Do not return out of a try body.
- `_Generic` types its controlling expression and does not evaluate it, but type checks every branch: select a function, then call it. `_Static_assert` for layout. `ARRAY_SIZE` must reject pointers. `min` and `max` evaluate once and warn on mixed types.
- VLA: stack storage, no failure report. Safe use is the pointer to a VLA type in a parameter. `-Wvla`.
- Threads: a data race is undefined behaviour. Use atomics: relaxed for counters, release and acquire for publishing, seq_cst by default. Run threaded tests with `-fsanitize=thread`. x86 hides ordering bugs. Keep per-thread data on separate cache lines (`_Alignas(64)`).
- Signals: set a `volatile sig_atomic_t` flag and call only async-signal-safe functions. Save `errno`.
- Extensions: `section` with `__start_` and `__stop_`, `noreturn`, `always_inline`, `weak`, `unused`, `used`, `__builtin_expect` (small effect, measure), `__builtin_unreachable` (a promise).
- Floating point: no `==`, NaN is unequal to itself, a narrow accumulator stops growing, float to int out of range is undefined, `-ffast-math` removes NaN checks.
- C23: `nullptr`, `constexpr`, `typeof`, `auto`, attributes, `bool`, `unreachable()`; GCC 13.3 lacks `_BitInt` and `#embed`.

---

## Sources

- The `format`, `noreturn`, `always_inline` and `section` function attributes ([GCC manual, Common Function Attributes](https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html)) and the `cleanup` variable attribute ([GCC manual, Common Variable Attributes](https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html)). These two pages could not be read from here (the fetch returned only a link stub), so the links are unchecked and the attribute behaviour in this tutorial rests on the programs I ran.
- Statement expressions and `typeof` ([GCC manual](https://gcc.gnu.org/onlinedocs/gcc/Statement-Exprs.html), checked in C 09).
- Signal safety, the list of async-signal-safe functions, checked ([signal-safety(7)](https://man7.org/linux/man-pages/man7/signal-safety.7.html)).
- `setjmp` and `longjmp`, including the rule for modified locals, checked ([setjmp(3)](https://man7.org/linux/man-pages/man3/setjmp.3.html)).
- C23 support by GCC version, checked ([GCC C status page](https://gcc.gnu.org/projects/c-status.html)).
- C 09 for ownership and the failing allocator, and C 07 for the bit builtins. C 11 (robust and secure C) continues from here.
