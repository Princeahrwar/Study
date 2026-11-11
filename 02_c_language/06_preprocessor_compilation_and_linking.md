# C 06: The Preprocessor, Compilation and Linking

*Part of the [C language plan](00_plan.md) · Time: about 4 hours (2.5 reading, 1.5 problems) · You need: day-to-day C, a Linux shell with `gcc`, `nm`, `readelf` and `objdump`, [C 03](03_memory_stack_heap_and_malloc.md) for the memory map and [C 05](05_qualifiers_storage_classes_and_linkage.md) for linkage. There is no companion demo file: every program and command below was run, and its real output is printed under it*

A C program is not what the compiler sees, and the executable is not what the compiler makes. Between your source and a running process there is a chain: the preprocessor rewrites text, the compiler turns each file into assembly, the assembler into an object file full of placeholders, the linker fills the placeholders in, and the loader maps the result into memory, resolves what is left and calls your code from a start-up routine that runs before `main`. Almost every "strange" build or start-up problem sits in one link of that chain: a macro that evaluates its argument twice, a header that defines instead of declares, a library listed in the wrong order, a constructor that runs before you expect, output that disappears on exit. This tutorial walks the chain in order and shows each stage with the real tool output, then covers the tools that change the result: macros done safely, weak symbols and `LD_PRELOAD` interposition, `dlopen`, symbol visibility, thread-local storage, and link-time, section-level and profile-guided optimisation.

By the end you can:

- name the stages of a build (preprocess, compile, assemble, link, load) and say which tool and which file each one produces;
- predict the result of a macro, spot the double evaluation and the missing parentheses, write macros that are safe (`do { } while (0)`, statement expressions, a pointer-rejecting `ARRAY_LEN`) and use an X-macro to keep a table, an enum and a name list in step;
- read the sections of an object file (`.text`, `.rodata`, `.data`, `.bss`), a relocation (`R_X86_64_PLT32`) and the letters of `nm`;
- say how the linker resolves symbols (strong and weak, static archives pulled left to right, shared libraries at load time) and fix an `undefined reference` caused by library order;
- build a static library and a shared library, say why a shared object needs `-fPIC`, read `ldd` and the `NEEDED` entries, load a plugin with `dlopen` and `dlsym`, and read the errors;
- interpose a libc function with `LD_PRELOAD`, override a weak symbol, and control exported symbols with `-fvisibility=hidden`;
- list what runs before and after `main` (`_start`, `__libc_start_main`, constructors, `atexit` handlers, destructors), explain `exit` against `_exit` against `abort`, and explain why `_exit` can lose buffered output;
- explain thread-local storage (`.tdata`, the `%fs` register) and show per-thread values;
- say what link-time optimisation, `-ffunction-sections` with `--gc-sections` and profile-guided optimisation do, with measured results (including the ones that did not help).

**Where the numbers come from.** Every program and command was run on an x86-64 Linux container (Ubuntu, glibc 2.39, GCC 13.3, binutils 2.42 as shipped there; PIE executables are the default). The macros need GNU extensions (statement expressions, `typeof`, `##__VA_ARGS__`), so the code in this tutorial is compiled with `-std=gnu11 -Wall -Wextra`; the earlier tutorials used `-std=c11`. The helper code printed in the tutorial (the safe macros, the X-macro table and the exit-handler list) is the real source, tested against oracles: 240,068 checks passed under AddressSanitizer and UBSan, including checks that every macro argument is evaluated exactly once and that handlers run last-in-first-out. I tried 14 deliberate bugs in it (an `STR` that does not expand its argument, a `SWAP` without `do { } while (0)`, a `MAX` that evaluates twice, an exit list that runs first-in-first-out, a table entry with the wrong number, and others); all 14 were caught, 13 by the tests and one (the `SWAP` without `do { } while (0)`) by the compiler refusing to build the test, which is exactly the lesson of section 2. The tool outputs are what `gcc`, `ld`, `nm`, `readelf`, `objdump`, `ldd` and `gdb` printed; I removed temporary object names (shown as `OBJ`), absolute paths, load addresses and the tool-specific columns I do not discuss. Statements about ARM64, Clang, musl, Windows, macOS (Mach-O) and exact internals of the dynamic loader are from memory and marked.

**Data and timing.** Machine: a cloud container, Intel Xeon 2.10 GHz, GCC 13.3.0, `-O2`, one thread. The three optimisation measurements at the end are best of 3 wall-clock runs with millisecond resolution, shown as run 1 / run 2. Timings show the shape of the cost on this machine and will differ on yours.

---

## 1. The build pipeline

`gcc hello.c -o hello` runs four programs. You can stop after any of them:

| Stage | Tool | Command to stop here | Output | What changed |
|---|---|---|---|---|
| Preprocess | `cpp` | `gcc -E hello.c` | `hello.i` | text only: `#include` pasted in, macros expanded, `#if` resolved, comments removed |
| Compile | `cc1` | `gcc -S hello.c` | `hello.s` | C to assembly for the target |
| Assemble | `as` | `gcc -c hello.c` | `hello.o` | machine code in an **object file**, with placeholders for addresses it cannot know |
| Link | `ld` (through `collect2`) | `gcc hello.c -o hello` | `hello` | objects and libraries joined, addresses assigned, references resolved |

The sizes at each stage for a six-line program:

```c
#include <stdio.h>

int main(void) {
    puts("hello");
    return 0;
}
```

```text
hello.c : 6 lines
hello.i : 819 lines  (after the preprocessor)
hello.s : 45 lines  (assembly)
hello.o : 1496 bytes (object file)
hello   : 15960 bytes (linked, dynamic)
```

Six lines became 819 after the preprocessor, because `#include <stdio.h>` pasted in the declarations of a standard header; 45 lines of assembly; a 1.5 KB object file; a 16 KB executable that still calls `puts` in the shared C library. The compiler works on **one file at a time** (a **translation unit**: the file after preprocessing), which is why a function defined in another file is only a name to it. Whatever the compiler cannot know is left as a **relocation** for the linker. This is the relocation table and the call in `hello.o`:

```text
$ readelf -r hello.o   (relocations, first rows)

Relocation section '.rela.text' at offset 0x198 contains 2 entries:
    Offset             Info             Type               Symbol's Value  Symbol's Name +
000000000000000b 0000000300000002 R_X86_64_PC32          0000000000000000 .rodata - 4
0000000000000013 0000000500000004 R_X86_64_PLT32         0000000000000000 puts - 4


$ objdump -dr hello.o   (the call to puts)
0000000000000000 <main>:
lea 0x0(%rip),%rax # f <main+0xf>
b: R_X86_64_PC32 .rodata-0x4
call 17 <main+0x17>
13: R_X86_64_PLT32 puts-0x4
```

The `call` instruction in the object file has a zero placeholder for its target; the relocation entry beside it says "at offset 0x13, patch in the address of `puts` (type `R_X86_64_PLT32`, which means: go through the procedure linkage table, minus 4 because the offset is relative to the next instruction)". The `lea` that loads the address of the string has its own relocation (`R_X86_64_PC32` against `.rodata`: a position-relative reference to the read-only data section). The linker looks at every relocation, decides where each section will sit in the final file and fills in the numbers. The relocation type names are the architecture's; ARM64 has others (`R_AARCH64_CALL26` and similar, from memory).

The result is an **ELF** file (Executable and Linkable Format; macOS uses Mach-O and Windows PE, from memory). An ELF file has two views of the same bytes: **sections** (`.text`, `.data`, ... used by the linker and tools) and **segments** (loadable ranges described by the program headers, used by the kernel and the loader). `readelf -S` shows sections, `readelf -l` shows segments.

## 2. The preprocessor

The preprocessor knows nothing about C. It reads text, follows lines that start with `#`, and replaces macros by token substitution. It runs before the compiler sees a single declaration, which gives it two properties that cause most of its bugs: it **does not understand types or scope**, and it **substitutes the text of the arguments**, not their values.

| Directive | What it does |
|---|---|
| `#include <f>` / `#include "f"` | paste the file; the quoted form searches the including file's directory first, then the same places as the angle form (`-I` adds directories) |
| `#define NAME text` / `#define F(a) text` | object-like and function-like macros; `#undef` removes one |
| `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif` | conditional compilation: the false branch is removed before the compiler sees it; `#if 0` comments out code that contains comments |
| `#error`, `#warning` | stop or warn at preprocessing time (`#warning` is common, not standard C) |
| `#pragma` | compiler-specific; `#pragma once` is widely supported but not standard |
| `#line`, predefined macros | `__FILE__`, `__LINE__`, `__DATE__`, `__STDC_VERSION__`; `__func__` is a variable, not a macro |

**Include guards** stop a header being pasted twice into one translation unit: `#ifndef FOO_H`, `#define FOO_H`, the contents, `#endif`. They solve nothing about definitions across files (C 05, section 5): a guard works inside one `.c` file, and the link-time rule needs `extern` declarations in headers. Command-line macros (`-DDEBUG=1`, `-UNAME`) and `-include file.h` change a build without editing the source. `-E` shows what the compiler will receive, and is the first thing to try when a macro misbehaves.

### 2.1 Macro pitfalls, with output

```c
#include <stdio.h>
#include "../code/mac.h"

static int calls;
static int val(int v) { ++calls; return v; }

int main(void) {
    printf("SQ_BAD(1 + 2) = %d   SQ(1 + 2) = %d\n", SQ_BAD(1 + 2), SQ(1 + 2));
    calls = 0;
    int m = MAX_BAD(val(5), val(3));
    printf("MAX_BAD(val(5), val(3)) = %d, val() was called %d times\n", m, calls);
    calls = 0;
    m = MAX(val(5), val(3));
    printf("MAX(val(5), val(3))     = %d, val() was called %d times\n", m, calls);
    printf("STR_(__LINE__) = %s, STR(__LINE__) = %s\n", STR_(__LINE__), STR(__LINE__));
    int arr[10];
    printf("ARRAY_LEN(arr) = %zu\n", (size_t)ARRAY_LEN(arr));
    return 0;
}
```

```text
SQ_BAD(1 + 2) = 5   SQ(1 + 2) = 9
MAX_BAD(val(5), val(3)) = 5, val() was called 3 times
MAX(val(5), val(3))     = 5, val() was called 2 times
STR_(__LINE__) = __LINE__, STR(__LINE__) = 15
ARRAY_LEN(arr) = 10
```

- **Missing parentheses.** `SQ_BAD(1 + 2)` becomes `1 + 2 * 1 + 2`, which is 5. Parenthesise every use of a parameter and the whole body.
- **Double evaluation.** `MAX_BAD(val(5), val(3))` expands to `val(5) > val(3) ? val(5) : val(3)`, calling `val` three times. A macro argument with a side effect (`i++`, a function call that counts, a `getc`) runs once per use in the expansion. The statement-expression version stores each argument in a local first, so each is evaluated exactly once (2 calls in the output). The `MAX` here is a GNU extension (`({ ... })` and `__typeof__`); the standard-C alternative is a `static inline` function for one type, or `_Generic` for several (C 10).
- **Stringify and paste need two levels.** `#x` turns the argument into a string **without expanding it**; `a##b` pastes tokens, also without expanding them. `STR_(__LINE__)` is `"__LINE__"`; the wrapper `STR(x)` expands `x` first and then calls `STR_`, giving `"15"`. The same two-level trick applies to `CAT`.
- **No scope, no types.** A macro named `max` or `min` replaces every identifier `max` in the file after it, including a local variable or a struct member from another header. Use upper case for macros and keep them out of public headers where you can. Prefer `static inline` functions (typed, scoped, debuggable) and `enum` constants over macros wherever they do the job.
- **Macros are invisible to the debugger** unless you compile with `-g3`; a breakpoint cannot be set "in" a macro.

### 2.2 Multi-statement macros and `do { } while (0)`

A macro that expands to several statements breaks `if` without braces. This real error comes from a `SWAP_BAD` written as `int t = a; a = b; b = t;` and used as the body of an `if` with an `else`:

```c
#define SWAP_BAD(a, b) int t = a; a = b; b = t;

int f(int c, int x, int y) {
    if (c)
        SWAP_BAD(x, y);
    else
        x = 0;
    return x + y;
}
```

```text
p1b_swap_bad.c:1:24: error: expected expression before 'int'
p1b_swap_bad.c:1:46: error: 't' undeclared (first use in this function)
p1b_swap_bad.c:6:5: error: 'else' without a previous 'if'
```

The expansion is `if (c) int t = x; x = y; y = t;; else ...`: only the first statement belongs to the `if`, the second `;` ends the `if`, and the `else` has nothing to attach to. Wrapping the body in `do { ... } while (0)` makes it a single statement that needs a trailing semicolon, exactly like a function call, and works before an `else`. The `SWAP` below is the safe form; the mutant that drops the `do`/`while` was rejected by the compiler when the test code used it in `if (...) SWAP(x, y); else ...`.

### 2.3 Safe macros, printed

```c
/* Stringify and paste. The two-level forms expand their argument first. */
#define STR_(x) #x
#define STR(x)  STR_(x)
#define CAT_(a, b) a##b
#define CAT(a, b)  CAT_(a, b)

/* The textbook mistakes, kept for comparison: no parentheses, and a double evaluation. */
#define SQ_BAD(x)     x * x
#define MAX_BAD(a, b) a > b ? a : b

/* Parenthesise every parameter and the whole body. Still evaluates its argument twice. */
#define SQ(x) ((x) * (x))

/* GCC and Clang statement expressions: each argument is evaluated once, into a typed local. */
#define MAX(a, b) ({ __typeof__(a) a_ = (a); __typeof__(b) b_ = (b); a_ > b_ ? a_ : b_; })
#define MIN(a, b) ({ __typeof__(a) a_ = (a); __typeof__(b) b_ = (b); a_ < b_ ? a_ : b_; })

/* A multi-statement macro that behaves like one statement, even before an else. */
#define SWAP(a, b) do { __typeof__(a) t_ = (a); (a) = (b); (b) = t_; } while (0)

/* Compile-time check: a negative-width bit-field is an error, zero is the value 0. */
#define BUILD_BUG_ON_ZERO(e) ((int)sizeof(struct { int : (-!!(e)); }))
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]) + \
    BUILD_BUG_ON_ZERO(__builtin_types_compatible_p(__typeof__(a), __typeof__(&(a)[0]))))

/* Variadic logging into a buffer; ## drops the comma when there are no extra arguments. */
#define LOG(buf, size, fmt, ...) \
    snprintf((buf), (size), "[%d] " fmt, __LINE__, ##__VA_ARGS__)
```

`ARRAY_LEN` is the useful new idea. `sizeof(a) / sizeof((a)[0])` gives the wrong answer silently if `a` has become a pointer (a parameter declared `int a[]`, C 02), because it computes 8 divided by 4. The extra term `BUILD_BUG_ON_ZERO(...)` is 0 when `a` is an array and a negative-width bit-field, a compile error, when `a`'s type is the same as the type of `&a[0]`, which is true only for a pointer:

```c
#include "../code/mac.h"

int count(int *p) {
    return (int)ARRAY_LEN(p);            /* p is a pointer, not an array */
}
```

```text
mac.h:28:50: error: negative width in bit-field '<anonymous>'
```

The error message is cryptic (a "negative width in bit-field" at the macro), but it fires at compile time and points at the line. The Linux kernel's `ARRAY_SIZE` uses the same idea (from memory). The `LOG` macro shows the other GNU extension in common use: `##__VA_ARGS__` deletes the comma before it when no extra arguments are given, so `LOG(buf, n, "plain")` and `LOG(buf, n, "x=%d", 5)` both compile (C23 standardises `__VA_OPT__` for this; from memory).

### 2.4 X-macros

When the same list has to appear in several forms, an **X-macro** keeps one copy. The list is a macro that calls a placeholder `X` for each entry; each use defines `X` differently, expands the list, and undefines `X`:

```c
/* One table, several uses: the enum, the names and the messages cannot drift apart. */
#define ERRORS(X)                        \
    X(E_OK,    0,  "ok")                 \
    X(E_NOMEM, 12, "out of memory")      \
    X(E_BUSY,  16, "device busy")        \
    X(E_INVAL, 22, "invalid argument")

enum ErrCode {
#define X(name, num, msg) name = num,
    ERRORS(X)
#undef X
};

enum {
    ERR_COUNT = 0
#define X(name, num, msg) + 1
    ERRORS(X)
#undef X
};

const char *err_name(int code) {
    switch (code) {
#define X(name, num, msg) case name: return #name;
    ERRORS(X)
#undef X
    default: return "E_UNKNOWN";
    }
}

const char *err_msg(int code) {
    switch (code) {
#define X(name, num, msg) case name: return msg;
    ERRORS(X)
#undef X
    default: return "unknown error";
    }
}
```

One table gives the enum (with the values 0, 12, 16, 22), the count (`ERR_COUNT` is 4), the name string and the message. Adding an entry means adding one line; the enum, the two `switch` statements and the count cannot drift apart (the tests check the values, the names and the messages, and that no two names collide). This is how the Linux kernel's syscall tables, error name tables and many register lists are built (from memory). The cost is readability: `-E` output is the way to debug one.

## 3. Object files and sections

An object file is a set of **sections** plus a **symbol table** plus **relocations**. The sections you meet every day:

| Section | Holds | In the file? | In memory |
|---|---|---|---|
| `.text` | machine code | yes | read-only, executable |
| `.rodata` | constants, string literals | yes | read-only |
| `.data` | initialised globals and `static`s | yes | read-write |
| `.bss` | zero-initialised globals and `static`s | **no**, only its size | read-write, zero filled at start |

This program has an initialised array, a zero array, a `const` array and a string:

```c
#include <stdio.h>

int initialised[1000] = {1};            /* .data: 4000 bytes in the file */
int zeroed[1000];                       /* .bss: 4000 bytes, none in the file */
const int table[1000] = {1};            /* .rodata (the string below too) */
static const char msg[] = "section demo";

int main(void) {
    printf("%d %d %d %s\n", initialised[0], zeroed[0], table[0], msg);
    return 0;
}
```

```text
$ size p_sec
text data bss dec hex filename
5449 4616 4032 14097 3711 p_sec

$ readelf -S p_sec (the sections that matter)
.text    PROGBITS  size 0x000126
.rodata  PROGBITS  size 0x000fda
.data    PROGBITS  size 0x000fc0
.bss     NOBITS    size 0x000fc0
```

`size` reports `text` (code plus read-only data) 5,449, `data` 4,616 (the 4,000 byte array plus the loader's tables), `bss` 4,032 (the 4,000 byte zero array). The binary contains the 4,000 bytes of `initialised` and the 4,000 bytes of `table` (it must store those values), and none of the zeroed array. That is why a large zero-initialised array costs RAM but no flash or disk, and why an embedded project checks `size` after every build (Embedded track). The `.rodata` section is bigger than `table` because it also holds the string literals.

The **symbol table** lists the names and which section each is in (`nm`, with the letters of C 05, section 5.3). A symbol the object uses but does not define is listed as `U`; the linker's job is to find a definition for each one.

## 4. How the linker resolves symbols

The linker processes its inputs from left to right and keeps two sets: symbols defined so far and symbols referenced but not yet defined. The rules:

1. **Object files** (`.o`) are always included whole.
2. **Static libraries** (`.a`, an archive of objects) contribute only the **members that define a currently undefined symbol**, and only when the linker reaches them. A member whose symbols nobody needs is left out.
3. **Shared libraries** (`.so`) are not copied in: the executable records the library's name (a `NEEDED` entry) and the loader maps it at run time. At link time the linker only checks that each symbol exists.
4. A **strong** symbol (a function, an initialised variable) beats a **weak** one (`__attribute__((weak))`, or an uninitialised global under `-fcommon`); two strong definitions of one name are an error (`multiple definition`, C 05).

A tiny static library shows rules 1 and 2. `libtiny.a` holds three objects (`add3`, `mul3` and a big `unused_big`) and `usemain.c` calls the first two:

```c
#include <stdio.h>
int add3(int);
int mul3(int);
int main(void) { printf("%d\n", mul3(add3(1))); return 0; }
```

```text
static  : 12   binary 16080 bytes
dynamic : 12   binary 16024 bytes, libtiny.so 15224 bytes
$ ldd use_dynamic
  linux-vdso.so.1
  libtiny.so => .../libtiny.so
  libc.so.6 => .../libc.so.6
  /lib64/ld-linux-x86-64.so.2
hello.c linked with -static: 785384 bytes (dynamic: 15960 bytes)
$ readelf -d use_dynamic | grep NEEDED
(NEEDED)             Shared library: [libtiny.so]
(NEEDED)             Shared library: [libc.so.6]
$ nm use_static | grep -E " (add3|mul3|unused_big)$"
T add3
T mul3
```

Both programs print 12. In the statically linked executable `nm` finds `add3` and `mul3` and **no** `unused_big`: the linker took two archive members and left the third. (Static here means a static *library*: the executable still uses the shared C library. `gcc -static` links libc in too, and the size above shows what that costs: the `-static` hello world is about fifty times larger.) The dynamic version records `libtiny.so` and `libc.so.6` as `NEEDED`; `ldd` lists what the loader will map, including the dynamic loader itself (`ld-linux-x86-64.so.2`) and the `vdso`, a small kernel-provided shared object that the kernel maps into every process for fast system calls like `clock_gettime` (from memory).

**Order matters for archives**, because of rule 2. If the library comes before the object that needs it, no symbol is undefined when the linker reads the archive, so it takes nothing, and the references that appear afterwards have nothing to resolve:

```text
$ gcc libtiny.a usemain.o   (library listed BEFORE the object that needs it)
usemain.c:(.text+0xe): undefined reference to `add3'
usemain.c:(.text+0x15): undefined reference to `mul3'
collect2: error: ld returned 1 exit status
$ gcc usemain.o libtiny.a
12
```

This is the cause of most "undefined reference" errors that survive after you have added the right library: put objects first, libraries after them, and if two libraries need each other, list one twice or wrap them in `-Wl,--start-group ... --end-group`.

### 4.1 Weak symbols

A **weak** definition is a default that any strong definition replaces, and a **weak reference** to a function that is not linked in resolves to address 0 instead of failing the link. Libraries use weak definitions for hooks and defaults; embedded start-up code uses them for interrupt handlers (`Default_Handler` aliased weakly to each vector, which an application overrides by defining a function of the same name).

```c
#include <stdio.h>
__attribute__((weak)) const char *hook_name(void) { return "default hook (weak)"; }
extern void optional(void) __attribute__((weak));
void run(void) {
    printf("hook: %s\n", hook_name());
    if (optional) optional(); else printf("optional() is not linked in\n");
}
```

```c
#include <stdio.h>
const char *hook_name(void) { return "strong override"; }
void optional(void) { printf("optional() was linked in and called\n"); }
```

```text
$ gcc weak_lib.o weak_main.o
hook: default hook (weak)
optional() is not linked in
$ gcc weak_lib.o weak_main.o weak_override.c
hook: strong override
optional() was linked in and called
```

Without `weak_override.c` the weak `hook_name` is used and `optional` is null, so the program takes the "not linked in" branch. With the strong definitions linked, `hook_name` returns the override and `optional()` is called.

### 4.2 Interposition with `LD_PRELOAD`

The dynamic loader searches for each symbol in a fixed order: the executable, then the libraries named in `LD_PRELOAD`, then the `NEEDED` libraries in order. The first definition wins, so a preloaded library can replace a function from libc **without recompiling the program**. The shim below defines its own `getpid`, finds the real one with `dlsym(RTLD_NEXT, ...)` (the next definition after this library in the search order) and returns a fixed number:

```c
#include <stdio.h>
#include <unistd.h>
int main(void) {
    printf("getpid() returned %d\n", getpid() == 4242 ? 4242 : -1);
    return 0;
}
```

```c
#define _GNU_SOURCE
#include <dlfcn.h>
#include <unistd.h>
#include <stdio.h>

pid_t getpid(void) {                     /* same name as the libc function: found first */
    pid_t (*real)(void) = (pid_t (*)(void))dlsym(RTLD_NEXT, "getpid");
    fprintf(stderr, "shim: getpid called (the real one returns %s)\n", real() > 0 ? "a positive number" : "nonsense");
    return 4242;
}
```

```text
$ ./target
getpid() returned -1
$ LD_PRELOAD=./shim.so ./target
shim: getpid called (the real one returns a positive number)
getpid() returned 4242
```

The program printed `-1` for its own test (the real pid is not 4242) and `4242` with the shim loaded, and the shim's message shows it really called the original. This is how `libSegFault`, malloc tracers, `fakeroot`, `faketime`, tcmalloc-by-preload and many test harnesses work (from memory). Limits: it does not work for statically linked programs, for setuid programs (the loader ignores `LD_PRELOAD` there), or for calls the compiler inlined or that bypass the PLT. It is also an attack surface, which is why hardened systems restrict it.

### 4.3 Visibility: what a shared library exports

By default every non-`static` function and global in a shared library is **exported**: visible to the loader's symbol search and to `dlsym`, and open to interposition. That exposes internals, slows loading and stops the compiler binding calls directly. `-fvisibility=hidden` flips the default, and `__attribute__((visibility("default")))` marks the API:

```c
int internal_helper(int x) { return x + 1; }
__attribute__((visibility("default"))) int api_call(int x) { return internal_helper(x) * 2; }
```

```text
$ nm -D --defined-only vis1.so   (default visibility)
T api_call
T internal_helper
$ nm -D --defined-only vis2.so   (-fvisibility=hidden)
T api_call
```

With default visibility the library exports `internal_helper` as well as `api_call`; with `-fvisibility=hidden` only the marked function is exported. A hidden symbol can still be called from inside the library; it just does not appear in the dynamic symbol table. Large C and C++ libraries use a macro like `MYLIB_API` for the attribute (and `__declspec(dllexport)` on Windows). Hidden symbols also let the compiler call them without going through the procedure linkage table (from memory), a measurable gain in call-heavy libraries.

## 5. Libraries, position-independent code and the loader

### 5.1 `-fPIC`

A shared library is mapped at an address the loader chooses, possibly different in each process, so its code cannot contain absolute addresses. **Position-independent code** refers to its own data by an offset from the instruction pointer and to external data through the **global offset table** (GOT). Without `-fPIC` the linker refuses to make a shared object when the code references a global that could be replaced by another definition:

```text
$ gcc -c glob.c; gcc -shared glob.o   (compiled without -fPIC)
glob_pie.o: warning: relocation against `counter' in read-only section `.text'
glob_pie.o: relocation R_X86_64_PC32 against symbol `counter' can not be used when making a shared object; recompile with -fPIC
collect2: error: ld returned 1 exit status
$ gcc -fPIC -shared glob.c
links
```

(Modern Ubuntu builds executables as PIE by default, so the object was already close to position independent; the error appears because `counter` is an exported global, and a reference to it must go through the GOT in a shared object so that it can be interposed.) The fix is to compile the objects of a shared library with `-fPIC`; a static library linked into an executable does not need it, and building a static library with `-fPIC` is common when the same objects feed both. On x86-64 the cost of PIC is small (about one extra memory access for external data); on 32-bit x86 it was larger (from memory).

### 5.2 Lazy binding, the PLT and the GOT

A call to a function in a shared library goes through a small stub in the **procedure linkage table** (PLT), which jumps through a slot in the **GOT**. Traditionally the slot first points back into the dynamic loader, which resolves the name on the **first call** and then patches the slot, so later calls jump straight to the function: **lazy binding**. Linking with `-z now` (or setting `LD_BIND_NOW=1`) resolves everything at start-up instead, which costs a little start-up time and closes the writable-GOT attack surface; many distributions build with it by default (from memory). This is why the relocation in `hello.o` is `R_X86_64_PLT32`.

### 5.3 Where the loader looks

The loader finds each `NEEDED` library by, in this order (from `ld.so(8)`, from memory): the `RPATH` recorded in the binary (if there is no `RUNPATH`), the `LD_LIBRARY_PATH` environment variable, the `RUNPATH`, the cache built by `ldconfig` (`/etc/ld.so.cache`) and the default directories (`/lib`, `/usr/lib`). The demo above linked with `-Wl,-rpath,'$ORIGIN'`, which means "the directory of the executable itself", so `use_dynamic` finds `libtiny.so` next to it. A program that works on your machine and fails elsewhere with `error while loading shared libraries` is missing a library, or the loader is not looking where you think: `ldd` shows what it will find.

### 5.4 `dlopen`: loading code at run time

`dlopen` loads a shared object **by name at run time** and `dlsym` looks up a symbol in it: the mechanism behind plugins, language runtimes and optional features. This host loads a plugin, calls a function in it, then provokes both of the common errors:

```c
int plugin_run(int x) { return x * 10; }
```

```c
#include <dlfcn.h>
#include <stdio.h>

int main(void) {
    void *h = dlopen("./plugin.so", RTLD_NOW);
    if (!h) { printf("dlopen failed: %s\n", dlerror()); return 1; }
    int (*run)(int) = (int (*)(int))dlsym(h, "plugin_run");
    printf("plugin_run(4) = %d\n", run ? run(4) : -1);
    void *missing = dlsym(h, "no_such_function");
    printf("dlsym(no_such_function) = %s\n", missing ? "found" : dlerror());
    dlclose(h);
    void *nofile = dlopen("./absent.so", RTLD_NOW);
    printf("dlopen(absent.so): %s\n", nofile ? "opened" : dlerror());
    return 0;
}
```

```text
plugin_run(4) = 40
dlsym(no_such_function) = ./plugin.so: undefined symbol: no_such_function
dlopen(absent.so): ./absent.so: cannot open shared object file: No such file or directory
```

`dlsym` returns a `void *`; converting it to a function pointer is not strictly portable C (POSIX requires it to work), and the cast is the usual form. `dlerror()` returns a readable message and clears the error state, so call it once per failure. `RTLD_NOW` resolves every symbol of the library immediately (errors show at load); `RTLD_LAZY` defers function symbols until first use. Since glibc 2.34 the `-ldl` flag is no longer needed (the functions moved into libc; I kept the flag in the build line and it is harmless).

## 6. What runs before and after `main`

`main` is not the first function to run. The kernel starts the process at the ELF entry point, `_start`, which calls `__libc_start_main`. That function initialises libc (including stdio and thread-local storage), runs the **constructors**, calls `main`, and calls `exit` with the return value. `exit` runs the functions registered with `atexit` in reverse order, then the **destructors**, flushes and closes the stdio streams, and finally calls `_exit`, a system call that ends the process. You can see this with a program that announces each step:

```c
#include <stdio.h>
#include <stdlib.h>

static void say(const char *s) { printf("%s\n", s); }
__attribute__((constructor(102))) static void ctor_b(void) { say("constructor 102"); }
__attribute__((constructor(101))) static void ctor_a(void) { say("constructor 101"); }
__attribute__((destructor)) static void dtor(void) { say("destructor"); }
static void exit1(void) { say("atexit handler 1 (registered first)"); }
static void exit2(void) { say("atexit handler 2 (registered second)"); }

int main(void) {
    say("main starts");
    atexit(exit1);
    atexit(exit2);
    say("main returns");
    return 0;
}
```

```text
constructor 101
constructor 102
main starts
main returns
atexit handler 2 (registered second)
atexit handler 1 (registered first)
destructor
```

- `__attribute__((constructor))` functions run before `main`; the optional number is a priority: 101 to 65535, lower first, and constructors without a number run after the numbered ones. (The number 101 to 65535 range is GCC's; below 101 is reserved for the implementation. From memory.) They are stored in the `.init_array` section and the destructors in `.fini_array` (from memory).
- `atexit` handlers run last registered first: handler 2 before handler 1. The standard guarantees room for at least 32; the `ExitList` below is that structure as plain data:

```c
/* What atexit does, as a plain data structure: a stack of functions, run last in, first out. */
enum { MAX_EXIT = 32 };
typedef void (*ExitFn)(void);
typedef struct {
    ExitFn fns[MAX_EXIT];
    int n;
} ExitList;

int exit_register(ExitList *l, ExitFn f) {
    if (!f || l->n >= MAX_EXIT) return -1;
    l->fns[l->n++] = f;
    return 0;
}

void exit_run(ExitList *l) {
    while (l->n > 0) {
        ExitFn f = l->fns[--l->n];
        f();                                 /* the most recently registered runs first */
    }
}
```

- Destructors run after the `atexit` handlers, as the output shows.

`gdb` confirms the call chain. This is the backtrace at a breakpoint inside the constructor (addresses removed):

```text
#0  ctor_a at start.c:6
#1  call_init at ../csu/libc-start.c:145
#2  __libc_start_main_impl at ../csu/libc-start.c:347
#3  _start
```

The constructor is called by `call_init`, called by `__libc_start_main_impl`, called by `_start`. C++ static objects with constructors and dynamic initialisers are run by the same mechanism before `main`, and the order between **different** translation units is unspecified: the static initialisation order fiasco (C++ track). In C, static variables can only be initialised with constants, so there is no such problem, but GCC constructors reintroduce it: do not make a constructor depend on another file's constructor having run, unless you give priorities.

### 6.1 `exit`, `_exit` and `abort`

| Call | `atexit` handlers | Destructors | stdio flushed | Notes |
|---|---|---|---|---|
| `return` from `main` | yes | yes | yes | same as `exit(value)` |
| `exit(n)` | yes | yes | yes | |
| `_exit(n)` | no | no | **no** | immediate system call; used after `fork` in the child so the parent's buffers are not flushed twice |
| `abort()` | no | no | no | raises `SIGABRT`; may produce a core dump |

The "stdio flushed" column is the one that bites. `printf` writes into a buffer owned by the C library; when standard output is a file or a pipe the buffer is only written when it is full, when `fflush` is called, or at `exit`. So an `_exit` (or a crash) loses it:

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char **argv) {
    printf("buffered text with no newline");
    if (argc > 1 && argv[1][0] == 'u') _exit(0);
    exit(0);
}
```

```text
--- exit(0), stdout redirected to a file:
file contains: [buffered text with no newline]
--- _exit(0), stdout redirected to a file:
file contains: []
```

With `exit(0)` the text reaches the file; with `_exit(0)` the file is empty. On a terminal stdout is line buffered and the text would appear only if it ended in a newline. C 07 covers buffering and the `fork` double-print in detail.

## 7. Thread-local storage

`_Thread_local int x;` (C11; `__thread` in GCC) gives every thread its own copy of `x`. The compiler puts the initial values in a **TLS template**, the sections `.tdata` (initialised) and `.tbss` (zero), and when a thread starts the runtime allocates a block and copies the template into it. On x86-64 Linux the thread pointer lives in the `%fs` segment register and an access is one instruction.

```c
#include <pthread.h>
#include <stdio.h>

_Thread_local int per_thread = 7;
int shared_global = 7;

int read_tls(void) { return per_thread; }

static void *worker(void *arg) {
    (void)arg;
    per_thread += 100;
    shared_global += 100;
    return NULL;
}

int main(void) {
    pthread_t t;
    pthread_create(&t, NULL, worker, NULL);
    pthread_join(t, NULL);
    printf("after the worker ran: per_thread = %d (main's copy), shared_global = %d\n", per_thread, shared_global);
    return 0;
}
```

```text
after the worker ran: per_thread = 7 (main's copy), shared_global = 107
```

The worker added 100 to both variables. `shared_global` is one object, so main sees 107. `per_thread` is a separate copy in each thread, so main's copy is still 7. This is the code the compiler generated to read `per_thread`, and the section that holds its initial value:

```text
$ objdump -d tls | (read_tls)
 endbr64
 push %rbp
 mov %rsp,%rbp
 mov %fs:0xfffffffffffffffc,%eax
 pop %rbp
 ret
$ readelf -S tls | grep tdata/tbss
.tdata  PROGBITS  size 0x000004
```

`mov %fs:0xfffffffffffffffc,%eax` reads the 4 bytes at offset -4 from the thread pointer: one instruction, no function call, which is the **local-exec** model, available when the variable is defined in the executable. Code in a shared library cannot know its offset at link time, so it uses slower models: **initial-exec** (an offset from the GOT) or **general-dynamic** (a call to `__tls_get_addr`) (from memory; Drepper's TLS paper in the sources describes all of them). `errno` is the best-known thread-local variable (a macro that expands to `(*__errno_location())`). On ARM64 the thread pointer is in the `tpidr_el0` register (from memory). Thread-local storage is also why a `static` buffer made `_Thread_local` fixes thread safety of a helper such as `strtok` but not the overwrite problem of C 05, section 7.

## 8. Optimising across files, sections and runs

Three build-time tools change the result of linking. I measured each on a program built to show it, and two of the three did not give the speed-up the folklore promises.

### 8.1 Link-time optimisation (LTO)

Normally the compiler sees one file and cannot inline a function defined in another. `-flto` stores the compiler's intermediate form in the object files and runs the optimiser again at link time over the whole program, so inlining, dead-code removal and constant propagation work across files. The test: a loop of 400 million calls to `tiny(int, int)` defined in a different file:

```text
T1 400000000 iterations calling tiny() defined in another file: result without LTO 1400000000, with LTO 1400000000; calls to tiny() left in main: 1 without LTO, 0 with LTO
```

| 400,000,000 calls | Time ms (run 1 / run 2) |
|---|---|
| `-O2` | 214.00 / 206.00 |
| `-O2 -flto` | 232.00 / 232.00 |

LTO did what it promises: the call to `tiny` disappeared from `main` (one call without LTO, none with). It did **not** make this loop faster; it was a little slower in both runs. A single add per iteration, on a loop whose iterations depend on each other, is limited by that dependency, not by the call, so removing the call bought nothing here. I have not established why the LTO build is slower (a different code layout is likely); the point is that "inlining" is not "faster" by definition. LTO's practical value is smaller code, cross-file dead-code removal and inlining of larger functions, with a cost in build time and memory. For `-flto` to work, every object and the link step must use it, and `-O2` must be passed at link time too.

### 8.2 `-ffunction-sections` and `--gc-sections`

A linker normally keeps or drops a whole `.text` section per object file. `-ffunction-sections` puts each function in its own section, and the linker option `-Wl,--gc-sections` removes sections that nothing reachable refers to. The test: a file with 400 functions, only one of which is called:

```text
T2 400 functions, only fn0 is called: object file text 32207 bytes; linked default 56528 bytes (text 36697); with -ffunction-sections and --gc-sections 15856 bytes (text 1424); output 135 and 135
```

The default link kept all 400 functions (36,697 bytes of text, a 56,528 byte file). With the two options the unused ones were removed: 1,424 bytes of text and a 15,856 byte file, about a quarter of the size and 4 percent of the code. Embedded projects use this by default (with `-fdata-sections` for variables) because flash is scarce and library code is full of functions you never call. It works only on sections that are not referenced: a function reachable through a function pointer table is kept, which is why dead drivers disappear from firmware only when their table entry is also gone.

### 8.3 Profile-guided optimisation (PGO)

PGO builds the program once with instrumentation (`-fprofile-generate`), runs it on representative input to record which branches and calls are hot, and rebuilds with `-fprofile-use`, so the compiler lays out code and chooses inlining with real frequencies. The test: a small bytecode loop in which two opcodes make up most of the steps:

```text
T3 300000000 interpreter steps: result plain 2803848295, with profile 2803848295
```

| 300,000,000 interpreter steps | Time ms (run 1 / run 2) |
|---|---|
| `-O2` | 445.00 / 412.00 |
| `-O2 -fprofile-use` | 416.00 / 409.00 |

The result is the same; the time differs by a few percent in one run and not at all in the other, which is inside the measurement noise at millisecond resolution. **No reliable speed-up** on this program and this CPU. PGO is known to help large, branchy programs with a skewed distribution (compilers, databases, interpreters; Firefox and Chrome build with it, from memory) by a double-digit percentage, and it needs a representative training run and a rebuild; a tiny loop whose branch predictor already learns the pattern is the wrong test. Measure your own program before adding it to a build.

## 9. What it costs

| Question | Answer (this machine, GCC 13.3) |
|---|---|
| `#include <stdio.h>` | 819 lines of preprocessed text for a six-line program; compile time, no run-time cost |
| A macro against a `static inline` function | the same code at `-O2`; the function is typed, scoped and debuggable |
| Object file for `hello.c` | 1,496 bytes; the executable is 15,960 bytes (dynamic), far more with `-static` |
| Zero-initialised array in `.bss` | 4,000 bytes of RAM, none in the file; an initialised one costs both |
| Static library member pulled in | only if it defines a needed symbol, and only if the library is listed after the user |
| Shared library call (PLT) | one extra jump; the first call also pays for symbol lookup unless bound at start-up |
| `dlopen` | file mapping and symbol resolution at run time; use for plugins, not for hot paths |
| TLS access in an executable | one instruction (`%fs` offset); in a shared library a call to `__tls_get_addr` or a GOT load |
| `-flto` | cross-file inlining and dead-code removal; no speed-up on my loop; longer builds |
| `--gc-sections` with `-ffunction-sections` | 56,528 to 15,856 bytes on a program with 399 unused functions |
| PGO | no reliable change on my loop; helps large branchy programs (from memory) |

## 10. Common mistakes

1. **A macro argument with a side effect.** `MAX(i++, j)` with a macro that evaluates twice increments twice. Use a statement-expression macro or a function.
2. **No parentheses.** `#define SQ(x) x * x` and `SQ(1 + 2)`. Parenthesise every parameter and the whole expansion.
3. **A multi-statement macro without `do { } while (0)`.** It works until someone writes it under an `if` without braces.
4. **`sizeof(a) / sizeof(a[0])` on a parameter.** It is a pointer; the answer is 2 on a 64-bit machine. Use a checked `ARRAY_LEN`.
5. **`#define` for constants and types.** `enum` and `const` and `typedef` have types and scope; a `#define` constant does not.
6. **Libraries before objects on the link line.** `undefined reference` for symbols the library really has. Objects first, libraries last.
7. **A definition in a header.** `multiple definition` at link time (C 05). Declare with `extern` in the header, define once.
8. **Forgetting `-fPIC` for a shared library.** The linker error says `recompile with -fPIC`.
9. **Exporting everything from a shared library.** Internals become API, interposable and slower to load. Use `-fvisibility=hidden` and mark the API.
10. **`_exit` or a crash with unflushed stdio.** Output disappears. `fflush` before `_exit`, or use `exit`.
11. **A constructor that depends on another file's initialisation.** Order across files is unspecified unless you give priorities.
12. **`LD_PRELOAD` or `dlopen` with a relative path and the wrong directory.** The loader searches its own list; pass `./lib.so` or an absolute path, or set `RPATH` with `$ORIGIN`.

## 11. Questions out loud

1. **What happens between `gcc hello.c` and a running process?** Preprocess, compile to assembly, assemble to an object file with relocations, link into an ELF executable; then the kernel maps it, the dynamic loader maps and binds the shared libraries, and `_start` runs `__libc_start_main`, constructors, `main`, `exit`.
2. **What does the preprocessor do, and what can't it do?** Text substitution: includes, macros, conditionals. It has no types or scope, and substitutes argument text, which is why macros double-evaluate and need parentheses.
3. **Why `do { } while (0)` in a macro?** To make several statements one statement that needs a semicolon and works in an unbraced `if`/`else`.
4. **What is an X-macro?** One list macro that takes a placeholder `X`, expanded several times with different `X` definitions to generate an enum, a name table and so on from one source of truth.
5. **What is in an object file?** Sections (`.text`, `.data`, `.bss`, `.rodata`), a symbol table, and relocations that mark every address the compiler could not know.
6. **What does the linker do?** Assigns addresses to sections, resolves each undefined symbol to a definition (objects whole, archive members only when needed, shared libraries recorded for the loader), applies the relocations, and complains about duplicates and unresolved names.
7. **What is the difference between a static and a shared library?** A static library (`.a`) is an archive of objects copied into the executable at link time (only the needed members). A shared library (`.so`) is mapped at run time, shared between processes, and needs position-independent code.
8. **Why does link order matter?** Archives are scanned once, left to right, and give up only members that define a symbol already undefined. A library listed before its user supplies nothing.
9. **What are weak symbols?** Definitions or references that yield to a strong definition and, for a weak reference, resolve to null if absent. Used for default handlers and optional features.
10. **How does `LD_PRELOAD` work?** The loader searches preloaded libraries before the others, so a preloaded definition replaces the library's; `dlsym(RTLD_NEXT)` reaches the original.
11. **What runs before `main`, and after?** `_start`, `__libc_start_main`, libc initialisation, constructors; after `main` returns, `exit` runs the `atexit` handlers in reverse, then destructors, flushes stdio, and calls `_exit`.
12. **What is the difference between `exit`, `_exit` and `abort`?** `exit` runs handlers and flushes; `_exit` ends the process at once without either; `abort` raises `SIGABRT` without either.
13. **What is thread-local storage, and how is it implemented?** One copy per thread, initialised from a `.tdata`/`.tbss` template; on x86-64 reached through the `%fs` thread pointer.
14. **What do LTO, `--gc-sections` and PGO do?** Whole-program optimisation at link time, removal of unreferenced sections, and optimisation from measured run-time frequencies. Measure each: LTO and PGO gave me no speed-up on small loops; `--gc-sections` cut an unused-function program to a quarter of its size.

---

## Problems

Difficulty is marked E (entry level), M (medium) or H (hard). Do A to C on paper first.

**A (E, on paper).** Given `#define DOUBLE(x) x + x`, `#define MAX_BAD(a, b) a > b ? a : b`, `#define STR_(x) #x`, `#define STR(x) STR_(x)` and `#define VALUE 6`: what do `3 * DOUBLE(4)`, `2 * MAX_BAD(3, 4)`, `MAX_BAD(1, 2) + 10`, `STR_(VALUE)` and `STR(VALUE)` give? Write the corrected `DOUBLE` and `MAX`.

**B (E, on paper).** Predict the exact output of this program, line by line (it calls `exit(0)` at the end of `main`):

```text
static int global = 5;
static void a(void) { printf("a\n"); }
static void b(void) { printf("b\n"); }
constructor           static void c1(void) { printf("c1 global=%d\n", global); }
constructor(150)      static void c2(void) { printf("c2\n"); }
destructor            static void d1(void) { printf("d1\n"); }
int main(void) { atexit(a); atexit(b); printf("main\n"); exit(0); }
```

**C (M, on paper).** Say what happens, and why, for each build: (1) `gcc libtiny.a usemain.o` where `usemain.o` calls `add3` from the library; (2) a program that defines `const char *hook_name(void)` strongly in one file and weakly in a library it links; (3) a shared library built from objects compiled without `-fPIC` that reference an exported global; (4) a program run with `LD_PRELOAD=./shim.so` where the shim defines `getpid` and the program is statically linked.

**D (M, in your editor).** Use an X-macro to define six commands (`name`, `handler function`, `help text`), and generate from the one table: an enum of command ids, a dispatch array of function pointers, a table of names, and a `help()` function that prints every command and its help text. Write a small parser that looks a command up by name and calls its handler, and test that an unknown name is rejected and that adding a seventh command is a one-line change.

**E (H, in your editor).** Write `libmalloccount.so`, a shim that counts calls to `malloc` and `free` and prints the totals when the program exits (use a destructor), and run it on `ls` with `LD_PRELOAD`. Use `dlsym(RTLD_NEXT, "malloc")` for the real functions. Watch for the trap: `dlsym` can itself call `calloc` or `malloc` while you are initialising, which recurses into your shim, so guard against re-entry and serve the first requests from a small static buffer. Report what you found for `ls` and `ls -l /usr/lib`.

**Where to solve them.** A to C on paper first, then check against the answers. D and E in your editor: D with `gcc -std=gnu11 -Wall -Wextra`, E with `gcc -shared -fPIC -o libmalloccount.so shim.c -ldl`. The programs in sections 4 and 6 are the templates.

**How to approach them.**

- **A:** write out the text each macro expands to, then apply the precedence of `*`, `>`, `?:` and `+`. Do not evaluate the macro as if it were a function.
- **B:** list the order: constructors (numbered first, lowest first, then unnumbered), `main`, `atexit` handlers in reverse, destructors.
- **C:** for each, name the rule from section 4 that applies (archive member pulled only when needed, strong beats weak, `-fPIC` for exported globals, `LD_PRELOAD` needs the dynamic loader).
- **D:** write the table first, then one `#define X` per generated artefact, `#undef X` after each; check with `gcc -E` that the expansions are what you meant.
- **E:** write the shim without the guard first and watch it recurse or crash, then add the guard; print with `write(2, ...)` and not `printf` from inside `malloc`, because `printf` can call `malloc`.

## Answers

**A.** (Program output below.) `3 * DOUBLE(4)` expands to `3 * 4 + 4`, which is 16, not 24. `2 * MAX_BAD(3, 4)` expands to `2 * 3 > 4 ? 3 : 4`; `*` binds tighter than `>`, so it is `(2 * 3 > 4) ? 3 : 4`, which is 3, not 8. `MAX_BAD(1, 2) + 10` expands to `1 > 2 ? 1 : 2 + 10`: the `+ 10` attaches to the 2 in the else branch, so the result is 12 (it would also be 12 with correct parentheses here, by coincidence of the numbers). `STR_(VALUE)` is the string `"VALUE"` (no expansion), `STR(VALUE)` is `"6"`. Corrected: `#define DOUBLE(x) ((x) + (x))`, and `MAX` as the statement-expression form of section 2.3 (or `((a) > (b) ? (a) : (b))` if the arguments have no side effects).

```text
3 * DOUBLE(4) = 16
2 * MAX_BAD(3, 4) = 3
MAX_BAD(1, 2) + 10 = 12
STR_(VALUE) = VALUE, STR(VALUE) = 6
after SWAP_TWO: x = 2, y = 1
```

**B.** The output of the real program:

```text
c2
c1 global=5
main
b
a
d1
```

Constructors first, the numbered one (150) before the unnumbered one; the `c1` global is already 5 because it is a constant initialiser, set before any code runs. Then `main`. Then `exit(0)` runs the `atexit` handlers in reverse registration order (`b`, then `a`), then the destructor `d1`. Calling `exit` or returning from `main` gives the same order.

**C.** (1) The link fails with `undefined reference to add3`: the library is scanned before `usemain.o` introduces the undefined symbol, so the linker pulls no member (section 4; the `order` output). Put the library after the object. (2) The strong definition wins and the weak one is ignored; no error. (3) The link fails: `relocation R_X86_64_PC32 against symbol 'counter' can not be used when making a shared object; recompile with -fPIC` (the `pic` output). (4) The shim has no effect: a statically linked program has no dynamic loader and no symbol search at run time, so `LD_PRELOAD` is ignored (from memory; also true for setuid programs).

**D.** An outline, not a full solution. `#define COMMANDS(X) X(CMD_ADD, add, "add two numbers") ...` (six lines). Enum: `enum { #define X(id, fn, help) id, COMMANDS(X) #undef X CMD_COUNT };`. Handlers: `static int (*const handlers[CMD_COUNT])(const char *) = { #define X(id, fn, help) cmd_##fn, ...}`; names: `#define X(id, fn, help) #fn,`. `help()` loops with a fourth expansion that prints `#fn` and the help string. The parser compares the name against the name table (a loop over `CMD_COUNT`) and calls `handlers[i]`. The paste `cmd_##fn` needs the handler functions to exist with the matching names, which the compiler checks for you. A seventh command is one new line in `COMMANDS` plus its handler function; the enum, the arrays and the help text follow.

**E.** An outline. In the shim keep `static void *(*real_malloc)(size_t)`, `static unsigned long mallocs, frees` and a `static __thread int in_init` guard. On first use set `real_malloc = dlsym(RTLD_NEXT, "malloc")`; if `dlsym` itself calls `malloc` while `in_init` is set, return memory from a static 4 KB arena (an arena is C 03). Count only after initialisation, make the counters atomic if the target is multi-threaded, and print in a `__attribute__((destructor))` with `snprintf` into a stack buffer and a single `write(2, ...)`. A typical `ls` makes a few hundred `malloc` calls; `ls -l` on a larger directory makes thousands, as it allocates for each entry and for user and group name lookups (from memory; the numbers depend on the system and the directory). The match between the counts of `malloc` and `free` is a first look at leaks; ASan and Valgrind (C 03) do this properly.

## Cheat sheet

- **Pipeline:** `gcc -E` (preprocess) → `-S` (assembly) → `-c` (object) → link (executable) → load (kernel, dynamic loader, `_start`).
- **Macros:** parenthesise every parameter and the whole body; arguments are text, so side effects repeat; `#x` and `a##b` need a two-level wrapper to expand first; multi-statement macros use `do { } while (0)`; prefer `static inline`, `enum` and `const`.
- **Safe macros (GNU):** `({ __typeof__(a) a_ = (a); ... })` for one evaluation; `ARRAY_LEN` with a compile-time pointer check; `##__VA_ARGS__` for optional arguments (`__VA_OPT__` in C23).
- **X-macro:** `#define LIST(X) X(a, ...) X(b, ...)`; `#define X(...) ...` `LIST(X)` `#undef X` once per generated form.
- **Sections:** `.text` code, `.rodata` constants, `.data` initialised, `.bss` zeroed (size only). `size` and `readelf -S` show them; `nm` letters T/t D/d B/b R/r U.
- **Relocations:** placeholders in the object file that the linker (or loader) patches; `R_X86_64_PLT32` for a call through the PLT.
- **Linking:** objects whole; archive members only when needed, left to right (libraries after objects); shared libraries recorded as `NEEDED`; strong beats weak; two strong definitions are an error.
- **Shared libraries:** `-fPIC`, `-shared`, `-fvisibility=hidden` plus `visibility("default")` for the API, `$ORIGIN` RPATH, `ldd` to see what the loader finds, `dlopen`/`dlsym`/`dlerror` for plugins.
- **Interposition:** `LD_PRELOAD` plus `dlsym(RTLD_NEXT, ...)`; not for static or setuid programs.
- **Start and stop:** `_start`, `__libc_start_main`, constructors (priority 101 up), `main`, `exit`: `atexit` reverse order, destructors, flush, `_exit`. `_exit` and `abort` skip handlers and flushing.
- **TLS:** `_Thread_local`, `.tdata`/`.tbss`, `%fs` on x86-64; one copy per thread.
- **Build optimisation:** `-flto` (cross-file), `-ffunction-sections -Wl,--gc-sections` (drop unused), PGO (`-fprofile-generate` then `-fprofile-use`). Measure each.

## Sources

- [GNU C preprocessor manual](https://gcc.gnu.org/onlinedocs/cpp/): macros, stringification, concatenation, variadic macros, predefined macros, `-E`.
- [GCC manual: Optimize Options](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html): `-flto`, `-ffunction-sections`, `-fprofile-generate` and `-fprofile-use`.
- [ld.so(8)](https://man7.org/linux/man-pages/man8/ld.so.8.html): how the dynamic loader finds libraries, `LD_PRELOAD`, `LD_LIBRARY_PATH`, `LD_BIND_NOW`.
- [dlopen(3)](https://man7.org/linux/man-pages/man3/dlopen.3.html): `dlopen`, `dlsym`, `dlerror`, `RTLD_NEXT`.
- [Ulrich Drepper: How To Write Shared Libraries](https://www.akkadia.org/drepper/dsohowto.pdf): PIC, symbol visibility, relocations, lazy binding (could not be fetched from here, so the link is unchecked; I did not re-read it for this tutorial).
- [Ulrich Drepper: ELF Handling For Thread-Local Storage](https://www.akkadia.org/drepper/tls.pdf): the TLS models (local-exec, initial-exec, general-dynamic); could not be fetched from here, so the link is unchecked.
