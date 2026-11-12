# C 09: Struct Design and Object-Oriented C

*Part of the [C language plan](00_plan.md) · Time: about 5 hours (3 reading, 2 problems) · You need: [C 02](02_pointers_arrays_and_functions.md) for pointers and function pointers, [C 03](03_memory_stack_heap_and_malloc.md) for the heap and the sanitizers, and [C 08](08_advanced_pointers_and_pointer_bugs.md) for pointer bugs and function pointers. There is no companion demo file: every program below was compiled and run, and its real output is printed under it*

C has no classes, yet the Linux kernel, the BSD network stack, OpenSSL, SQLite, and every device driver framework are object oriented. They get objects from **structs**, inheritance from **embedding one struct in another**, virtual functions from **tables of function pointers**, containers from **intrusive links** and **macros**, and encapsulation from **opaque types**. This tutorial teaches those patterns as working code. You will read the idioms that appear in kernel and driver source (`container_of`, `list_head`, `file_operations`), write them yourself, and learn the rules that keep them safe: what a struct copy really copies, who owns each allocation, and what a constructor must do when the third allocation fails.

By the end you can:

- predict `sizeof` and `offsetof` for a struct on x86-64, shrink one by reordering fields, and say why `memcmp` and `==` are both wrong for comparing structs;
- use designated initialisers, compound literals and anonymous unions, and say how long a compound literal lives;
- hide a struct behind an opaque type and say what that costs and buys;
- write a tagged union with a switch over its kind, and say what every node pays for;
- explain `container_of`, write the Linux style intrusive list, and put one object on two lists at once;
- build a vtable based polymorphic interface with optional operations, and read a kernel style `file_operations` table;
- use the BSD `sys/queue.h` macros, and generate type-safe containers with a macro;
- write constructors and destructors with exactly one owner per allocation, and unwind a failed constructor with `goto`;
- use flexible array members, and say what packing a struct costs;
- choose between array of structs and structure of arrays from measurements, and say why the answer depends on the access pattern.

**Where the numbers come from.** Every program and command was run on an x86-64 Linux container (Ubuntu, glibc 2.39, GCC 13.3). Code is compiled with `-std=gnu11 -Wall -Wextra -Wno-unused-function` (the header defines several `static` helpers and each demo uses only some). Sanitizer demos add `-g -O0 -fsanitize=address -fno-omit-frame-pointer`. The helper code printed here (the list, the shape and device tables, the expression tree, the generated stack, the packet constructor, and an opaque stack) is the real source and was tested against oracles: 4,357,647 checks passed under AddressSanitizer and UBSan, comparing each part with an independent model (a plain array for the lists, closed formulas for the shapes, an index ordered reference evaluator and printer for the trees, and a failing allocator that makes the 1st, 2nd or 3rd allocation fail). I tried 29 deliberate bugs in it (a list insert that links the wrong neighbour, a `list_del` that forgets the back link, a `container_of` that adds the offset, a `scale` called through a NULL table entry, a constructor that leaks the header when the payload allocation fails, and others). 28 were caught, 17 by a failed check and 11 by the sanitizers; one was equivalent, a printing helper that writes one byte too many and then overwrites it with the terminator, so no observable behaviour changes. Sanitizer output is shortened: the process id, addresses, and frames from the C library and the sanitizer are removed, and the rest is verbatim. Statements about the Linux kernel, the BSDs, ARM64, Clang and the ABI are from memory unless marked as checked, and say so.

**Data and timing.** Machine: a cloud container, Intel Xeon 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Timings are the best of 3 wall-clock runs in milliseconds, shown as run 1 / run 2 (two complete repetitions of the whole benchmark). The loop is timed inside the program for the array comparison, so allocation and initialisation are excluded. They show the shape of the cost on this machine and will differ on yours.

---

## 1. Struct semantics

A struct is a block of named members laid out in declaration order, with padding inserted so that each member sits at an address that is a multiple of its alignment, and the whole struct has the alignment of its most aligned member. Four facts follow, and each shows up in interviews.

**Layout and reordering.** The compiler may not reorder members, so you can. Here are four structs of the same fields, compiled and measured:

```text
struct A {char a; double b; char c;}   size 24  offsets a=0 b=8 c=16
struct B {double b; char a; char c;}   size 16  offsets b=0 a=8 c=9
struct C {char tag; int n; short s;}   size 12  offsets tag=0 n=4 s=8
struct D {char tag; short s; int n;}   size 8  offsets tag=0 s=2 n=4
```

`struct A` wastes 7 bytes after `a` (to align the `double`) and 7 after `c` (so that arrays of `A` keep every `b` aligned). Putting the widest member first gives `struct B` at 16 bytes instead of 24. The rule of thumb: **order members from the largest alignment to the smallest**. The `C` and `D` pair shows the same effect with an `int` and a `short`: 12 bytes against 8. On x86-64 the sizes above are what GCC produced here; on a platform with different alignments they change (ARM64 and x86-64 agree on these, from memory). Reordering is an ABI change for a public struct, so do it before the layout is published, and check it with `offsetof` and `_Static_assert(sizeof(...) == N)`. Kernel developers use the `pahole` tool to find holes; I did not run it here.

**Copy, pass and return by value.** Assigning a struct copies every member, **including arrays**, but a pointer member copies the pointer and not what it points at:

```c
#include <stdio.h>
#include <string.h>

typedef struct {
    char name[8];
    int scores[3];
    int *shared;
} Rec;

typedef struct {
    char c;
    int i;
} Padded;

static Rec bump(Rec r)
{
    r.scores[0] += 100;
    return r;
}

int main(void)
{
    int common = 7;
    Rec a = {"ann", {1, 2, 3}, &common};
    Rec b = a;
    b.scores[1] = 99;
    strcpy(b.name, "bob");
    *b.shared = 8;
    printf("after b = a and edits to b:  a.scores[1]=%d  a.name=%s  *a.shared=%d (the pointer member was copied, not what it points at)\n", a.scores[1], a.name, *a.shared);

    Rec c = bump(a);
    printf("bump(a) returned a modified copy: c.scores[0]=%d, a.scores[0]=%d\n", c.scores[0], a.scores[0]);

    Padded p, q;
    memset(&p, 0xFF, sizeof p);
    memset(&q, 0x00, sizeof q);
    p.c = 1; p.i = 2;
    q.c = 1; q.i = 2;
    printf("p and q hold the same fields: %s;  memcmp(&p, &q, sizeof p) = %s\n",
           (p.c == q.c && p.i == q.i) ? "yes" : "no", memcmp(&p, &q, sizeof p) == 0 ? "0 (equal)" : "non-zero (different)");
    printf("sizeof(Padded) = %zu, the fields use 5 bytes, the other 3 are padding with unspecified contents\n", sizeof(Padded));
    return 0;
}
```

```text
after b = a and edits to b:  a.scores[1]=2  a.name=ann  *a.shared=8 (the pointer member was copied, not what it points at)
bump(a) returned a modified copy: c.scores[0]=101, a.scores[0]=1
p and q hold the same fields: yes;  memcmp(&p, &q, sizeof p) = non-zero (different)
sizeof(Padded) = 8, the fields use 5 bytes, the other 3 are padding with unspecified contents
```

The first two lines show it: `b = a` followed by edits to `b` leaves `a.scores` and `a.name` alone (they were copied) but `*b.shared = 8` changed what `a.shared` points at too (a **shallow copy**). Section 8 comes back to this when the struct owns memory. Passing a struct as an argument or returning one copies it the same way, which is why `bump(a)` changed only its own copy.

**Comparison.** C has no `==` for structs, and the compiler says so:

```c
typedef struct { int x, y; } Pt;

int same(Pt a, Pt b)
{
    return a == b;
}
```

```text
d1b_eq_bad.c:5:14: error: invalid operands to binary == (have 'Pt' and 'Pt')
```

The tempting replacement is `memcmp(&p, &q, sizeof p)`. The last two lines of the first program show it is wrong: `p` and `q` hold the same fields, but `memcmp` says they differ, because **padding bytes have unspecified contents** (here 3 bytes after `c`; `p` was filled with `0xFF` and `q` with zero before the fields were set). The same trap breaks hashing a struct by its bytes and writing a struct to a file or a socket and reading it back on another build. The fix is a comparison function that compares member by member, and for the wire a serialiser that writes each field at a defined offset in a defined byte order. For security, a struct copied to user space by a kernel must be zeroed first, or the padding leaks kernel stack bytes (from memory; it is a known class of kernel information leaks).

**Cost of passing by value.** For small structs the copy is free; for large ones it dominates. I called a function 10,000,000 times with a struct argument of 16, 64, 256 and 1024 bytes, by value and by `const` pointer. The functions are marked `noipa` (an attribute that stops GCC from specialising or inlining them): **without it GCC removed the copy for the 1024 byte case and both versions took about 16 ms**. That is a measurement lesson as much as a result: a benchmark of a call that the optimiser can see through measures the optimiser.

```text
T2 10000000 calls with a struct argument: checksums size 16 4520000000 and 4520000000; 64 4120000000 and 4120000000; 256 2520000000 and 2520000000; 1024 14840000000 and 14840000000
```

| Struct size (bytes) | By value (ms) | By pointer (ms) |
|---|---|---|
| 16 | 17 / 17 | 16 / 16 |
| 64 | 16 / 16 | 16 / 16 |
| 256 | 31 / 33 | 15 / 16 |
| 1024 | 191 / 205 | 16 / 16 |

Up to 64 bytes the two are indistinguishable here (on the x86-64 System V ABI a struct of up to 16 bytes travels in registers and larger ones are copied to the stack, from memory; at 64 bytes the copy is cheap enough to hide in the loop). At 256 bytes by value costs about twice the pointer version and at 1024 bytes about twelve times, 19 to 21 ns per call. The practical rule: **pass anything larger than two or three machine words by `const` pointer**, and return large structs through an output parameter. By value stays right for small immutable values such as a point, a timestamp, or a pair that is returned.

---

## 2. Initialisers, compound literals and anonymous members

Since C99 you can name the member you initialise (a **designated initialiser**), build an unnamed object in an expression (a **compound literal**), and since C11 you can leave a nested struct or union without a name (an **anonymous member**), so its members are used as if they belonged to the parent.

```c
#include <stdio.h>

typedef struct {
    const char *name;
    int x, y, z;
    union {
        int i;
        float f;
    };
    struct {
        unsigned char r, g;
    } color;
} Cfg;

static void show(const Cfg *c)
{
    printf("name=%s x=%d y=%d z=%d color=(%u,%u)\n", c->name ? c->name : "(null)", c->x, c->y, c->z, c->color.r, c->color.g);
}

static int sum(const int *v, int n)
{
    int s = 0;
    for (int i = 0; i < n; ++i)
        s += v[i];
    return s;
}

int main(void)
{
    Cfg a = {.name = "a", .y = 5, .color = {.g = 9}};
    show(&a);
    Cfg b = {"b", 1, 2, 3};
    show(&b);
    show(&(Cfg){.name = "temp", .z = 4});

    int table[8] = {[2] = 20, [5] = 50, 51};
    printf("table with designators: ");
    for (int i = 0; i < 8; ++i)
        printf("%d ", table[i]);
    printf("\n");
    printf("sum((int[]){1, 2, 3, 4}, 4) = %d\n", sum((int[]){1, 2, 3, 4}, 4));

    a.f = 1.5f;
    printf("anonymous union: set a.f = 1.5f, the same bytes read as a.i = 0x%x\n", a.i);
    return 0;
}
```

```text
name=a x=0 y=5 z=0 color=(0,9)
name=b x=1 y=2 z=3 color=(0,0)
name=temp x=0 y=0 z=4 color=(0,0)
table with designators: 0 0 20 0 0 50 51 0
sum((int[]){1, 2, 3, 4}, 4) = 10
anonymous union: set a.f = 1.5f, the same bytes read as a.i = 0x3fc00000
```

- **Designated initialisers.** `{.name = "a", .y = 5, .color = {.g = 9}}` sets those members and **zero-initialises every other member**, so `= {0}` or `= {}` zeroes the whole object (the empty form is standard from C23 and a GNU extension before that, from memory). Padding bytes are not guaranteed to be zero, as section 1 showed. Order does not matter, and a positional initialiser after a designator continues from that member: in `{[2] = 20, [5] = 50, 51}` the value 51 lands at index 6, as the output shows. Prefer designators for any struct with more than three members: adding a member in the middle of the struct cannot silently shift the values of a positional list. The `Cfg b = {"b", 1, 2, 3}` line in the program compiles but GCC warns about the missing initialisers for the union and the colour, which is one more reason to use designators.
- **Compound literals.** `(Cfg){.name = "temp", .z = 4}` is an lvalue, so `&(Cfg){...}` works and passes a temporary to a function (`show` above). `(int[]){1, 2, 3, 4}` gives an array whose size is counted for you. **Lifetime:** a compound literal inside a function has automatic storage that lasts until the end of the enclosing block, **not** the end of the statement as for a C++ temporary. Returning a pointer to one is the same dangling pointer bug as returning a pointer to a local array (C 08). At file scope it has static storage. C++ has no compound literals (GCC accepts them in C++ as an extension; from memory).
- **Anonymous members.** `a.f` and `a.i` above are members of an anonymous union: both name the same four bytes, and writing `1.5f` then reading `a.i` shows its bit pattern, `0x3fc00000`. In C, reading a union member other than the last one written reinterprets the bytes; in C++ it is undefined behaviour (from memory), which matters when you share a header between the two. Anonymous structs inside unions are how register overlays are written (a 32 bit register viewed as one word or as four bytes).

---

## 3. Opaque types: hiding the struct

An **opaque type** is a struct that is declared in the header and defined only in the `.c` file. Callers can hold a pointer and nothing more, so they cannot read members, cannot copy it, and cannot allocate it on their stack. The library controls every state the object can be in.

```c
#ifndef INTSTACK_H
#define INTSTACK_H
#include <stddef.h>

/* Public interface: the struct is declared but not defined, so a caller can
   hold a pointer to it and nothing more. */
typedef struct IntStack IntStack;

IntStack *intstack_new(void);                  /* NULL on failure; the caller owns the result */
void intstack_free(IntStack *s);               /* accepts NULL */
int intstack_push(IntStack *s, int v);         /* 0, or -1 when out of memory */
int intstack_pop(IntStack *s, int *out);       /* 0, or -1 when empty */
size_t intstack_size(const IntStack *s);

#endif
```

```c
#include "intstack.h"
#include <stdlib.h>

struct IntStack {
    int *data;
    size_t len, cap;
};

IntStack *intstack_new(void)
{
    return calloc(1, sizeof(IntStack));
}

void intstack_free(IntStack *s)
{
    if (!s)
        return;
    free(s->data);
    free(s);
}

int intstack_push(IntStack *s, int v)
{
    if (s->len == s->cap) {
        size_t ncap = s->cap ? s->cap * 2 : 4;
        int *p = realloc(s->data, ncap * sizeof *p);
        if (!p)
            return -1;
        s->data = p;
        s->cap = ncap;
    }
    s->data[s->len++] = v;
    return 0;
}

int intstack_pop(IntStack *s, int *out)
{
    if (s->len == 0)
        return -1;
    *out = s->data[--s->len];
    return 0;
}

size_t intstack_size(const IntStack *s)
{
    return s->len;
}
```

Here is a caller that tries to look inside:

```c
#include <stdio.h>
#include "intstack.h"

int main(void)
{
    IntStack *s = intstack_new();
    printf("%zu\n", s->len);
    IntStack copy = *s;
    intstack_free(s);
    (void)copy;
    return 0;
}
```

```text
use_bad.c:7:22: error: invalid use of incomplete typedef 'IntStack'
use_bad.c:8:5: error: variable 'copy' has initializer but incomplete type
use_bad.c:8:21: error: invalid use of incomplete typedef 'IntStack'
use_bad.c:8:14: error: storage size of 'copy' isn't known
```

The compiler refuses all three uses: reading `s->len`, copying `*s`, and declaring a variable of the type, because the type is incomplete. A correct caller:

```c
#include <stdio.h>
#include "opq/intstack.h"

int main(void)
{
    IntStack *s = intstack_new();
    for (int i = 1; i <= 5; ++i)
        intstack_push(s, i * i);
    printf("size after 5 pushes: %zu\n", intstack_size(s));
    int v;
    while (intstack_pop(s, &v) == 0)
        printf("popped %d\n", v);
    printf("pop on empty returns %d\n", intstack_pop(s, &v));
    intstack_free(s);
    return 0;
}
```

```text
size after 5 pushes: 5
popped 25
popped 16
popped 9
popped 4
popped 1
pop on empty returns -1
```

What it buys: **the layout can change without recompiling callers** (add a field, change the growth policy; a shared library keeps its ABI), **invariants cannot be broken from outside** (nobody can set `len` above `cap`), and the header documents the whole interface in six lines. What it costs: every object is a heap allocation reached through a pointer, accessors cannot be inlined across the library boundary unless you use link time optimisation, and the caller cannot place the object in static or stack storage. Libraries get the best of both with the pair `_init(T *)`/`_destroy(T *)` on caller provided storage when they can afford to publish the size (`pthread_mutex_t` does this, from memory), or they publish `sizeof` and the alignment as constants. `FILE` is used as an opaque type by convention, but glibc defines it in a public header, so the compiler does not enforce it (from memory).

**Handles.** The same idea with an integer: file descriptors, window ids and graphics API object names are indices into a table inside the library. A stale handle cannot dereference freed memory, it can only fail a lookup, and a library that adds a generation counter to the high bits can detect reuse of a closed handle. The cost is the lookup and the table's own lifetime rules.

---

## 4. Tagged unions: variants

A **tagged union** is a struct with a kind field and a union of the payloads, where the kind says which member is alive. It is the C form of a variant (`std::variant`, a Rust enum). The classic use is a tree of different node types, here an arithmetic expression:

```c
typedef enum { X_NUM, X_ADD, X_MUL, X_NEG } XKind;

/* A tagged union: the kind says which member of the union is alive. */
typedef struct Expr Expr;
struct Expr {
    XKind kind;
    union {
        long num;
        struct { Expr *l, *r; } bin;
        Expr *operand;
    } u;
};

static Expr *x_new(XKind k)
{
    Expr *e = calloc(1, sizeof *e);
    if (e)
        e->kind = k;
    return e;
}
static Expr *x_num(long v) { Expr *e = x_new(X_NUM); if (e) e->u.num = v; return e; }
static Expr *x_bin(XKind k, Expr *l, Expr *r)
{
    Expr *e = x_new(k);
    if (!e) {
        free(l);
        free(r);
        return NULL;
    }
    e->u.bin.l = l;
    e->u.bin.r = r;
    return e;
}
static Expr *x_neg(Expr *a) { Expr *e = x_new(X_NEG); if (e) e->u.operand = a; return e; }

static long x_eval(const Expr *e)
{
    switch (e->kind) {
    case X_NUM: return e->u.num;
    case X_ADD: return x_eval(e->u.bin.l) + x_eval(e->u.bin.r);
    case X_MUL: return x_eval(e->u.bin.l) * x_eval(e->u.bin.r);
    case X_NEG: return -x_eval(e->u.operand);
    }
    return 0;
}

static void x_free(Expr *e)
{
    if (!e)
        return;
    switch (e->kind) {
    case X_NUM: break;
    case X_ADD:
    case X_MUL: x_free(e->u.bin.l); x_free(e->u.bin.r); break;
    case X_NEG: x_free(e->u.operand); break;
    }
    free(e);
}

static size_t put_(char *buf, size_t size, size_t pos, const char *s)
{
    size_t n = strlen(s);
    if (size > 0 && pos < size - 1) {
        size_t room = size - 1 - pos;
        memcpy(buf + pos, s, n < room ? n : room);
    }
    return pos + n;
}

static size_t x_fmt_(const Expr *e, char *buf, size_t size, size_t pos)
{
    char tmp[32];
    switch (e->kind) {
    case X_NUM:
        snprintf(tmp, sizeof tmp, "%ld", e->u.num);
        return put_(buf, size, pos, tmp);
    case X_ADD:
    case X_MUL:
        pos = put_(buf, size, pos, "(");
        pos = x_fmt_(e->u.bin.l, buf, size, pos);
        pos = put_(buf, size, pos, e->kind == X_ADD ? " + " : " * ");
        pos = x_fmt_(e->u.bin.r, buf, size, pos);
        return put_(buf, size, pos, ")");
    case X_NEG:
        pos = put_(buf, size, pos, "(-");
        pos = x_fmt_(e->u.operand, buf, size, pos);
        return put_(buf, size, pos, ")");
    }
    return pos;
}

/* snprintf contract: always terminates, returns the full length. */
static size_t x_to_string(const Expr *e, char *buf, size_t size)
{
    size_t n = x_fmt_(e, buf, size, 0);
    if (size)
        buf[n < size ? n : size - 1] = '\0';
    return n;
}
```

```text
((2 + 3) * (-4)) = -20   (16 characters)
into an 8 byte buffer: "((2 + 3" and the full length 16 is returned, like snprintf
sizeof(Expr) = 24: every node pays for the largest alternative (two pointers) plus the kind
```

The discipline that makes this safe:

- **Every function that reads the union switches on the kind.** With `-Wall`, GCC warns when a `switch` over an enum misses a case, so adding a kind (`X_SUB`) points to every function that must change. Do not put a `default:` on such a switch, or that warning goes away.
- **The constructor sets the kind and the matching member together.** `x_bin` frees both children when its own allocation fails, so the caller never leaks a half built tree (ownership of the children passes to the node on success and on failure).
- **Recursion mirrors the type.** `x_free` frees the children first and the node last; reversing it leaks them or reads freed memory.
- **`x_to_string` follows the `snprintf` contract**: it always terminates when `size > 0`, never writes past the buffer, and returns the length the full result would have. The 8 byte buffer in the output holds `((2 + 3` and the return value 16 tells the caller it was truncated.

Every node is `sizeof(Expr) = 24` bytes, the size of the kind plus the largest alternative (two pointers), even for a plain number: a space cost of the single struct approach, paid to avoid a second level of allocation. A tree with millions of numbers might split them into a separate pool; a handful of nodes should not.

**Flexible array members.** The other layout trick for variable size objects is a trailing array with no size, from C99:

```c
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    unsigned n;
    char data[];          /* flexible array member: no storage in sizeof */
} Msg;

struct __attribute__((packed)) Wire {
    char tag;
    int n;
};

static Msg *msg_new(const char *s)
{
    size_t len = strlen(s) + 1;
    Msg *m = malloc(sizeof *m + len);
    if (!m)
        return NULL;
    m->n = (unsigned)len;
    memcpy(m->data, s, len);
    return m;
}

int main(void)
{
    printf("sizeof(Msg) = %zu, offsetof(Msg, data) = %zu\n", sizeof(Msg), offsetof(Msg, data));
    Msg *a = msg_new("hello"), *b = msg_new("xxxxxx");
    printf("a holds %u bytes \"%s\"; b holds %u bytes \"%s\"\n", a->n, a->data, b->n, b->data);
    *b = *a;
    printf("after *b = *a: b->n = %u but b->data is still \"%s\" (assignment copies only the fixed part)\n", b->n, b->data);
    free(a);
    free(b);
    printf("packed: sizeof(struct Wire) = %zu, offsetof(n) = %zu\n", sizeof(struct Wire), offsetof(struct Wire, n));
    return 0;
}
```

```text
sizeof(Msg) = 4, offsetof(Msg, data) = 4
a holds 6 bytes "hello"; b holds 7 bytes "xxxxxx"
after *b = *a: b->n = 6 but b->data is still "xxxxxx" (assignment copies only the fixed part)
packed: sizeof(struct Wire) = 5, offsetof(n) = 1
```

`sizeof(Msg)` excludes the array, `offsetof(Msg, data)` says where it starts, and you allocate `sizeof *m + len` bytes in one malloc so header and data share a cache line and one `free`. The catch is on the last line of the demo: **assigning a struct with a flexible member copies only the fixed part**, so `*b = *a` changed `b->n` and left `b->data` as it was. Never copy such an object by assignment; allocate a new one and `memcpy` the whole length. The same program shows the other layout lever, `packed`: the struct shrinks to 5 bytes with `n` at offset 1, but taking the address of the member now draws a warning from the compiler:

```text
d10w_bad.c:8:12: warning: taking address of packed member of 'struct Wire' may result in an unaligned pointer value [-Waddress-of-packed-member]
```

An unaligned `int *` may fault or run slowly on some CPUs, and a compiler may assume the pointer is aligned. x86-64 tolerates it; many embedded cores do not (from memory). For wire formats, read each field with `memcpy` into a local variable rather than dereferencing a pointer into a packed struct. Bit-field layout is implementation defined, so for hardware registers and protocol headers the masks and shifts of C 07 are more portable.

---

## 5. Inheritance by embedding, `container_of`, and intrusive lists

C inheritance is **embedding**: put the "base" struct inside the "derived" one. If the base is the **first member**, a pointer to the derived object and a pointer to its base have the same address, and the standard guarantees a pointer to a struct can be converted to a pointer to its first member. That gives you upcasts for free. For the downcast, or for a base that is not first, you subtract the member's offset: that is `container_of`.

```c
/* From a pointer to a member back to the object that contains it. */
#define container_of(ptr, type, member) ({                          \
    const __typeof__(((type *)0)->member) *mptr_ = (ptr);           \
    (type *)((char *)mptr_ - offsetof(type, member)); })

/* The Linux-kernel style intrusive circular doubly linked list. */
struct list_head {
    struct list_head *next, *prev;
};

#define LIST_POISON1 ((struct list_head *)0x100)
#define LIST_POISON2 ((struct list_head *)0x122)

static void INIT_LIST_HEAD(struct list_head *h)
{
    h->next = h;
    h->prev = h;
}

static void list_insert_(struct list_head *n, struct list_head *prev, struct list_head *next)
{
    next->prev = n;
    n->next = next;
    n->prev = prev;
    prev->next = n;
}

static void list_add(struct list_head *n, struct list_head *head) { list_insert_(n, head, head->next); }
static void list_add_tail(struct list_head *n, struct list_head *head) { list_insert_(n, head->prev, head); }

/* Unlink and poison: a later use of the entry faults at once instead of corrupting the list. */
static void list_del(struct list_head *e)
{
    e->prev->next = e->next;
    e->next->prev = e->prev;
    e->next = LIST_POISON1;
    e->prev = LIST_POISON2;
}

/* Unlink and re-initialise: safe to delete twice. */
static void list_del_init(struct list_head *e)
{
    e->prev->next = e->next;
    e->next->prev = e->prev;
    INIT_LIST_HEAD(e);
}

static int list_empty(const struct list_head *h) { return h->next == h; }

#define list_entry(ptr, type, member) container_of(ptr, type, member)

#define list_for_each(pos, head) for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_entry(pos, head, member)                                          \
    for (pos = list_entry((head)->next, __typeof__(*pos), member);                      \
         &pos->member != (head);                                                        \
         pos = list_entry(pos->member.next, __typeof__(*pos), member))

/* Safe against deleting pos inside the loop: n holds the next entry. */
#define list_for_each_entry_safe(pos, n, head, member)                                  \
    for (pos = list_entry((head)->next, __typeof__(*pos), member),                      \
         n = list_entry(pos->member.next, __typeof__(*pos), member);                    \
         &pos->member != (head);                                                        \
         pos = n, n = list_entry(n->member.next, __typeof__(*n), member))
```

How `container_of` works: `offsetof(type, member)` is the byte offset of the member in the type, so `(char *)ptr - offsetof(type, member)` is the address of the containing object. The statement expression `({ ... })` and `__typeof__` are GNU extensions (GCC and Clang accept them, ISO C does not). The first line of the macro assigns `ptr` to a pointer of the member's type, which makes the compiler **check that `ptr` really points at that kind of member**, and `const` is carried through. The kernel version has more checks (from memory).

**Intrusive lists.** A list in the style of the C++ standard library allocates a node that contains a pointer to your object. An **intrusive** list puts the links *inside* your object: no extra allocation, no separate node to free, a member removes itself in O(1) with no search, and one object can be on several lists at once by having several `list_head` members. The node knows nothing about the container type; `container_of` (spelled `list_entry`) gets back to the object.

```c
#include <stdio.h>
#include <stdlib.h>
#include "../code/oop.h"

typedef struct {
    int id;
    struct list_head ready;
    char name[8];
    struct list_head all;
} Task;

static Task *make(int id, const char *name)
{
    Task *t = calloc(1, sizeof *t);
    t->id = id;
    strcpy(t->name, name);
    INIT_LIST_HEAD(&t->ready);
    INIT_LIST_HEAD(&t->all);
    return t;
}

int main(int argc, char **argv)
{
    (void)argv;
    struct list_head ready, all;
    INIT_LIST_HEAD(&ready);
    INIT_LIST_HEAD(&all);
    const char *names[] = {"init", "sshd", "cron", "bash"};
    for (int i = 0; i < 4; ++i) {
        Task *t = make(i + 1, names[i]);
        list_add_tail(&t->all, &all);
        if (i != 1)
            list_add_tail(&t->ready, &ready);
    }
    Task *t, *n;
    printf("all tasks:   ");
    list_for_each_entry(t, &all, all)
        printf("%d:%s ", t->id, t->name);
    printf("\nready tasks: ");
    list_for_each_entry(t, &ready, ready)
        printf("%d:%s ", t->id, t->name);
    printf("\n");
    struct list_head *first = ready.next;
    Task *owner = container_of(first, Task, ready);
    printf("container_of(first ready node, Task, ready) -> task %d (%s); offsetof(ready)=%zu offsetof(all)=%zu\n", owner->id, owner->name, offsetof(Task, ready), offsetof(Task, all));
    printf("removing every task while iterating with the _safe form:\n");
    list_for_each_entry_safe(t, n, &all, all) {
        printf("  freeing %s\n", t->name);
        list_del(&t->all);
        if (!list_empty(&t->ready))
            list_del(&t->ready);
        if (argc > 1 && t->id == 3)
            printf("  (use after delete) next of the removed entry: %p\n", (void *)t->all.next->next);
        free(t);
    }
    printf("all list empty now: %s\n", list_empty(&all) ? "yes" : "no");
    return 0;
}
```

```text
all tasks:   1:init 2:sshd 3:cron 4:bash
ready tasks: 1:init 3:cron 4:bash
container_of(first ready node, Task, ready) -> task 1 (init); offsetof(ready)=8 offsetof(all)=32
removing every task while iterating with the _safe form:
  freeing init
  freeing sshd
  freeing cron
  freeing bash
all list empty now: yes
```

A `Task` is on two lists: `all` holds every task and `ready` holds the runnable ones (sshd is not ready). `container_of(first, Task, ready)` found task 1 from the address of its `ready` link: `ready` is at offset 8 and `all` at offset 32, so the same task has two different node addresses and the macro subtracts the right amount for each. The loop at the end uses `list_for_each_entry_safe`, which **reads the next entry before the body runs**, so the body may delete and free the current one. With plain `list_for_each_entry` the increment `pos->member.next` would read freed memory.

`list_del` stores two poison values in the removed entry (`0x100` and `0x122`, the values used by the kernel list, with an address offset added in some kernel configurations; from memory). A bug that touches an entry after deleting it then faults at once on an address that is plainly not a heap pointer, instead of silently corrupting the list. I made that bug on purpose (the demo has a branch that follows `t->all.next->next` after `list_del`) and ran it with AddressSanitizer:

```text
ERROR: AddressSanitizer: SEGV on unknown address ADDR (pc ADDR bp ADDR sp ADDR T0)
    #0 main d5_list.c:53
    #1 __libc_start_call_main libc_start_call_main.h:58
    #2 __libc_start_main_impl ../csu/libc-start.c:360
SUMMARY: AddressSanitizer: SEGV d5_list.c:53 in main
the faulting address, unmasked: 0x000000000100
```

The unmasked faulting address is `0x100`, `LIST_POISON1`: the report names the poison. When you see a crash at `0x100` or `0x122` in a kernel oops, it is a use of a deleted list entry. `list_del_init` is the alternative that re-initialises the node, so deleting twice is harmless and `list_empty(&node)` tells whether it is on a list (the kernel does the same; from memory). The price of being intrusive is ownership: a node must be removed from **every** list before its object is freed, which is why the demo deletes from `ready` before `free(t)`.

---

## 6. Vtables: polymorphism with function pointers

A **vtable** is a struct of function pointers shared by all objects of a type. Each object starts with a pointer to its table; calling a "virtual" function loads the pointer from the table and calls it. This is what a C++ compiler generates for `virtual`, written by hand.

```c
typedef struct Shape Shape;

/* The table of operations: one shared, constant instance per type. */
typedef struct ShapeOps {
    const char *name;
    double (*area)(const Shape *);
    double (*perimeter)(const Shape *);
    void (*scale)(Shape *, double);      /* optional: NULL means "not supported" */
    void (*destroy)(Shape *);
} ShapeOps;

struct Shape {
    const ShapeOps *ops;                 /* every object starts with its table */
};

typedef struct { Shape base; double w, h; } Rect;
typedef struct { Shape base; double r; } Circle;
typedef struct { Shape base; } Point;

static double rect_area(const Shape *s) { const Rect *r = container_of(s, Rect, base); return r->w * r->h; }
static double rect_perimeter(const Shape *s) { const Rect *r = container_of(s, Rect, base); return 2 * (r->w + r->h); }
static void rect_scale(Shape *s, double k) { Rect *r = container_of(s, Rect, base); r->w *= k; r->h *= k; }
static double circle_area(const Shape *s) { double r = container_of(s, Circle, base)->r; return 3.14159265358979323846 * r * r; }
static double circle_perimeter(const Shape *s) { return 2 * 3.14159265358979323846 * container_of(s, Circle, base)->r; }
static void circle_scale(Shape *s, double k) { container_of(s, Circle, base)->r *= k; }
static double point_zero(const Shape *s) { (void)s; return 0.0; }
static void shape_release(Shape *s) { free(s); }   /* valid because base is the first member */

static const ShapeOps rect_ops = {"rect", rect_area, rect_perimeter, rect_scale, shape_release};
static const ShapeOps circle_ops = {"circle", circle_area, circle_perimeter, circle_scale, shape_release};
static const ShapeOps point_ops = {"point", point_zero, point_zero, NULL, shape_release};
/* "Inheritance of implementation": a square reuses the rectangle's functions. */
static const ShapeOps square_ops = {"square", rect_area, rect_perimeter, rect_scale, shape_release};

static Shape *rect_new(double w, double h)
{
    Rect *r = malloc(sizeof *r);
    if (!r)
        return NULL;
    r->base.ops = &rect_ops;
    r->w = w;
    r->h = h;
    return &r->base;
}

static Shape *square_new(double side)
{
    Rect *r = malloc(sizeof *r);
    if (!r)
        return NULL;
    r->base.ops = &square_ops;
    r->w = r->h = side;
    return &r->base;
}

static Shape *circle_new(double radius)
{
    Circle *c = malloc(sizeof *c);
    if (!c)
        return NULL;
    c->base.ops = &circle_ops;
    c->r = radius;
    return &c->base;
}

static Shape *point_new(void)
{
    Point *p = malloc(sizeof *p);
    if (!p)
        return NULL;
    p->base.ops = &point_ops;
    return &p->base;
}

/* The polymorphic interface callers use. */
static double shape_area(const Shape *s) { return s->ops->area(s); }
static double shape_perimeter(const Shape *s) { return s->ops->perimeter(s); }
static int shape_scale(Shape *s, double k)
{
    if (!s->ops->scale)
        return -1;
    s->ops->scale(s, k);
    return 0;
}
static void shape_destroy(Shape *s)
{
    if (s)
        s->ops->destroy(s);
}

/* A kernel style "file operations" table: any entry may be NULL. */
typedef struct Dev Dev;
typedef struct DevOps {
    long (*read)(Dev *, char *buf, size_t n);
    long (*write)(Dev *, const char *buf, size_t n);
} DevOps;
struct Dev {
    const DevOps *ops;
    long state;
};
#define DEV_ENOSYS (-38)

static long dev_read(Dev *d, char *buf, size_t n) { return d->ops->read ? d->ops->read(d, buf, n) : DEV_ENOSYS; }
static long dev_write(Dev *d, const char *buf, size_t n) { return d->ops->write ? d->ops->write(d, buf, n) : DEV_ENOSYS; }

static long null_read(Dev *d, char *buf, size_t n) { (void)d; (void)buf; (void)n; return 0; }
static long null_write(Dev *d, const char *buf, size_t n) { (void)d; (void)buf; return (long)n; }
static long zero_read(Dev *d, char *buf, size_t n) { (void)d; memset(buf, 0, n); return (long)n; }
static long counter_read(Dev *d, char *buf, size_t n)
{
    int len = snprintf(buf, n, "%ld", d->state++);
    return len < 0 ? -1 : (len >= (int)n ? (long)n - 1 : len);
}
static const DevOps null_ops = {null_read, null_write};
static const DevOps zero_ops = {zero_read, null_write};
static const DevOps counter_ops = {counter_read, NULL};
static const DevOps sink_ops = {NULL, null_write};
```

```text
rect    area  12.000  perimeter  14.000
circle  area   3.142  perimeter   6.283
square  area   4.000  perimeter   8.000
point   area   0.000  perimeter   0.000
total area 19.142
scale rect by 2: rc=0, new area 48.000;  scale point: rc=-1 (no scale operation)
rect and square share an area function: yes;  they share an ops table: no
sizeof(Shape)=8 (just the table pointer), sizeof(Rect)=24, sizeof(Circle)=16
```

How it fits together:

- **The base is the first member** of `Rect`, `Circle` and `Point`, so a `Shape *` and the `Rect *` are the same address, and the implementation recovers its own type with `container_of` (a downcast that is checked by the compiler for the member type, not for the dynamic type). `shape_release` can call `free(s)` on the base pointer because the base is first.
- **One constant table per type**, `static const`, so it lives in read only memory and a corrupted object cannot redirect the calls except by overwriting its own table pointer.
- **Optional operations are NULL**, and the dispatcher checks: `point_ops.scale` is NULL and `shape_scale` returns -1 instead of crashing. The mutant that called through a NULL entry crashed the tests, which is why the check is in the dispatcher and not in every caller.
- **Inheritance of implementation** is sharing function pointers. `square_ops` reuses `rect_area`, `rect_perimeter` and `rect_scale` and changes only the name: the output shows the two types share an area function but not a table.
- **Cost.** The object pays one pointer (`sizeof(Shape) = 8`) and each call is one extra load and an indirect call. C 08 measured the call cost of an indirect call for predictable and unpredictable targets.

The same shape is the kernel's **operations table**. This is the pattern behind `struct file_operations` (a driver fills the entries it supports and the VFS calls through them; from memory), written as a small device with `read` and `write` where any entry may be missing:

```c
#include <stdio.h>
#include "../code/oop.h"

static void probe(const char *name, Dev *d)
{
    char buf[16];
    memset(buf, '#', sizeof buf);
    long r = dev_read(d, buf, sizeof buf - 1);
    long w = dev_write(d, "hello", 5);
    printf("%-8s read -> %3ld   write -> %3ld", name, r, w);
    if (r > 0 && buf[0] != 0)
        printf("   (read gave \"%.*s\")", (int)r, buf);
    printf("\n");
}

int main(void)
{
    Dev n = {&null_ops, 0}, z = {&zero_ops, 0}, c = {&counter_ops, 41}, k = {&sink_ops, 0};
    probe("null", &n);
    probe("zero", &z);
    probe("counter", &c);
    probe("counter", &c);
    probe("sink", &k);
    printf("DEV_ENOSYS is %d: the operation is missing from the table, and the dispatcher checks for NULL\n", DEV_ENOSYS);
    return 0;
}
```

```text
null     read ->   0   write ->   5
zero     read ->  15   write ->   5
counter  read ->   2   write -> -38   (read gave "41")
counter  read ->   2   write -> -38   (read gave "42")
sink     read -> -38   write ->   5
DEV_ENOSYS is -38: the operation is missing from the table, and the dispatcher checks for NULL
```

`null` and `zero` implement both operations; `counter` has `read` only, so `write` returns `-38`, which is `-ENOSYS` on Linux (from memory; the demo defines its own constant); `sink` has `write` only. The dispatcher turns a missing entry into an error code, **the contract of the kernel's tables as well**: some callbacks may be NULL and the core checks. A driver writer's job is to fill the table with `static const` data and to know which entries the core requires. Two related points: put the table pointer first in the object so generic code needs no knowledge of the derived type, and when the table is part of a public ABI, add new operations only at the end and give the table a size or version field, because an old driver compiled against the shorter table must not be called through an entry that does not exist (from memory; the kernel instead recompiles every in-tree driver).

---

## 7. Generic containers: `sys/queue.h` and macros

C has no templates. There are four ways to write a container once.

1. **Intrusive links** (section 5): nothing generic is needed, the object holds the node.
2. **`void *` plus an element size**: one copy of the code, `memcpy` to move elements, no type check. This is what `qsort` and `bsearch` do (C 08).
3. **Macros that stamp out one copy per type**: type safe, full speed, no casts.
4. **`_Generic`** to dispatch a macro on the argument type: C 10.

**BSD `sys/queue.h`** is method 3 for linked lists. It provides five list families (`SLIST`, `LIST`, `STAILQ`, `TAILQ`, `CIRCLEQ` in the classic header; check `queue(3)` on your system for what it has) all made of macros over members you declare yourself:

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/queue.h>

struct job {
    int id;
    TAILQ_ENTRY(job) link;
};
TAILQ_HEAD(joblist, job);

int main(void)
{
    struct joblist q;
    TAILQ_INIT(&q);
    for (int i = 1; i <= 4; ++i) {
        struct job *j = malloc(sizeof *j);
        j->id = i * 10;
        if (i % 2)
            TAILQ_INSERT_TAIL(&q, j, link);
        else
            TAILQ_INSERT_HEAD(&q, j, link);
    }
    struct job *j;
    printf("forward: ");
    TAILQ_FOREACH(j, &q, link)
        printf("%d ", j->id);
    printf("\nbackward: ");
    TAILQ_FOREACH_REVERSE(j, &q, joblist, link)
        printf("%d ", j->id);
    printf("\n");
    j = TAILQ_FIRST(&q);
    TAILQ_REMOVE(&q, j, link);
    free(j);
    printf("after removing the first, first is %d, empty? %s\n", TAILQ_FIRST(&q)->id, TAILQ_EMPTY(&q) ? "yes" : "no");
    while (!TAILQ_EMPTY(&q)) {
        j = TAILQ_FIRST(&q);
        TAILQ_REMOVE(&q, j, link);
        free(j);
    }
    printf("drained, empty? %s\n", TAILQ_EMPTY(&q) ? "yes" : "no");
    return 0;
}
```

```text
forward: 40 20 10 30
backward: 30 10 20 40
after removing the first, first is 20, empty? no
drained, empty? yes
```

You declare the link inside your struct with `TAILQ_ENTRY(job) link` and the head with `TAILQ_HEAD(joblist, job)`. What the macros produce, from the C preprocessor on this machine:

```text
struct job { int id; struct { struct job *tqe_next; struct job * *tqe_prev; } link; };
struct joblist { struct job *tqh_first; struct job * *tqh_last; };
```

The link is two pointers: `tqe_next` and **`tqe_prev`, a pointer to the previous element's `tqe_next`** (or to the head's `tqh_first`). That is why `TAILQ_REMOVE` needs no search and no special case for the first element: it writes `*elm->tqe_prev = elm->tqe_next` and fixes the back pointer of the successor. The singly linked `SLIST` has one pointer and a removal of an arbitrary element needs the predecessor. The `FOREACH` macros expand into plain `for` loops, so there is no function call overhead and `break` works. The glibc header has one gap that I measured: **`TAILQ_FOREACH_SAFE`, the BSD macro that allows deleting during iteration, is not defined here:**

```text
d4b_safe.c:6:2: error: #error "TAILQ_FOREACH_SAFE is not defined by this queue.h"
    6 | #error "TAILQ_FOREACH_SAFE is not defined by this queue.h"
```

On the BSDs it exists, as do the `_SAFE` forms in the Linux kernel's own `list.h` (section 5). With glibc you write the loop by hand: save `TAILQ_NEXT(j, link)` in a variable before the body. Do not rely on a BSD only macro in portable code without checking the header (from memory: glibc's `sys/queue.h` is copied from an older BSD; the measurement above is the only part I verified here).

**A macro generated container.** `DEFINE_STACK(NAME, T)` writes a stack of any element type, with the functions named `NAME_push`, `NAME_pop` and `NAME_free`. The macro body is ordinary C with `T` where the element type goes and `##` to build names:

```c
/* A type-safe container generated by a macro: one copy of the code per element type. */
#define DEFINE_STACK(NAME, T)                                                     \
    typedef struct { T *data; size_t len, cap; } NAME;                            \
    static int NAME##_push(NAME *s, T v)                                          \
    {                                                                             \
        if (s->len == s->cap) {                                                   \
            size_t nc = s->cap ? s->cap * 2 : 4;                                  \
            T *p = realloc(s->data, nc * sizeof *p);                              \
            if (!p)                                                               \
                return -1;                                                        \
            s->data = p;                                                          \
            s->cap = nc;                                                          \
        }                                                                         \
        s->data[s->len++] = v;                                                    \
        return 0;                                                                 \
    }                                                                             \
    static int NAME##_pop(NAME *s, T *out)                                        \
    {                                                                             \
        if (s->len == 0)                                                          \
            return -1;                                                            \
        *out = s->data[--s->len];                                                 \
        return 0;                                                                 \
    }                                                                             \
    static void NAME##_free(NAME *s)                                              \
    {                                                                             \
        free(s->data);                                                            \
        s->data = NULL;                                                           \
        s->len = s->cap = 0;                                                      \
    }

typedef struct { int x, y; } Pt;
DEFINE_STACK(IStack, int)
DEFINE_STACK(PStack, Pt)
```

`IStack` holds `int` and `PStack` holds a two-field struct, and the compiler checks every call: pushing a `Pt` onto an `IStack` is a type error, where the `void *` version compiles and corrupts memory. The costs of the macro approach: **the code is duplicated for each type** (small here), errors are reported inside the macro expansion with the line of the macro definition, a debugger steps into a single line of macro text, and a macro argument must not have side effects that the macro evaluates twice (the expansion above uses each argument once, by design). `static` keeps each instance private to the translation unit, so two files can instantiate the same name without a clash at link time. Use the macro form for small container families; use intrusive lists when objects belong on several lists and must not be copied.

---

## 8. Constructors, destructors and ownership

C has no constructors, so a convention does the job. **Every allocation has exactly one owner at any time, and the name or documentation of a function says what happens to ownership:**

- `T *t_new(...)` returns an owned object (or NULL); the caller must `t_free` it.
- `void t_free(T *)` takes ownership and releases it, accepts NULL, and frees what the object owns before the object itself.
- Functions that only look at an object take a `const T *` and keep nothing.
- A function that stores the pointer it is given says so (it takes ownership, or the caller must keep the object alive longer than the container).
- `T *t_clone(const T *)` or `t_copy(T *dst, const T *src)` makes a **deep copy**, the thing struct assignment does not do.

The hard part is a constructor that owns more than one resource. If the third allocation fails, the first two must be released, and the cleanest C for it is the `goto` unwinding ladder that the kernel and most system code use: one label per resource, released in reverse order of acquisition.

```c
typedef struct {
    char *header;
    char *payload;
    size_t n;
} Packet;

/* The allocator is a variable so a test can fail the 1st, 2nd or 3rd allocation. */
static void *(*pk_alloc)(size_t) = malloc;

/* Constructor with two resources: whichever step fails, everything already made is freed. */
static Packet *packet_new(size_t n)
{
    Packet *p = pk_alloc(sizeof *p);
    if (!p)
        return NULL;
    p->header = pk_alloc(16);
    if (!p->header)
        goto fail_packet;
    p->payload = pk_alloc(n ? n : 1);
    if (!p->payload)
        goto fail_header;
    memset(p->header, 0, 16);
    memset(p->payload, 0, n ? n : 1);
    p->n = n;
    return p;

fail_header:
    free(p->header);
fail_packet:
    free(p);
    return NULL;
}

static void packet_free(Packet *p)
{
    if (!p)
        return;
    free(p->payload);
    free(p->header);
    free(p);
}
```

The allocator `pk_alloc` is a variable so a test can make the 1st, 2nd or 3rd allocation fail:

```c
#include <stdio.h>
#include "../code/oop.h"

static int fail_at, made;
static void *flaky(size_t n)
{
    if (made++ == fail_at)
        return NULL;
    return malloc(n);
}

int main(void)
{
    pk_alloc = flaky;
    for (int k = 0; k < 4; ++k) {
        fail_at = k;
        made = 0;
        Packet *p = packet_new(100);
        printf("allocation %d fails: packet_new returned %s after %d allocation attempts\n", k + 1, p ? "a packet" : "NULL", made);
        packet_free(p);
    }
    return 0;
}
```

```text
allocation 1 fails: packet_new returned NULL after 1 allocation attempts
allocation 2 fails: packet_new returned NULL after 2 allocation attempts
allocation 3 fails: packet_new returned NULL after 3 allocation attempts
allocation 4 fails: packet_new returned a packet after 3 allocation attempts
```

Failing allocation 1 leaves nothing to free; failing 2 frees the packet; failing 3 frees the header and the packet (the two labels in order); allocation 4 never happens because the constructor makes three. Each failing case returned NULL. To see that the unwinding matters I removed the first `free(p->header);` line, which is the `fail_header` cleanup, and ran the same test under LeakSanitizer:

```text
ERROR: LeakSanitizer: detected memory leaks
Direct leak of 16 byte(s) in 1 object(s) allocated from:
    #1 flaky d9_packet.c:9
    #2 packet_new oop.h:349
    #3 main d9_packet.c:18
    #4 __libc_start_call_main libc_start_call_main.h:58
SUMMARY: AddressSanitizer: 16 byte(s) leaked in 1 allocation(s).
```

The leak is 16 bytes in 1 object (the header) and is reported only for the case that fails the third allocation. This is also the leak that testing with `malloc` that never fails does not find, which is why the failing allocator is part of the test. The mutant that did this was caught by the sanitizer in the oracle test.

**Shallow copy meets ownership.** Section 1 showed that struct assignment copies pointers. When the struct owns what it points at, that is a **double free** waiting to happen:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    int score;
} Rec;

static Rec rec_new(const char *name, int score)
{
    Rec r;
    r.name = malloc(strlen(name) + 1);
    strcpy(r.name, name);
    r.score = score;
    return r;
}

static void rec_free(Rec *r) { free(r->name); }

int main(void)
{
    Rec a = rec_new("ann", 5);
    Rec b = a;
    b.score = 6;
    printf("%s %d, %s %d\n", a.name, a.score, b.name, b.score);
    rec_free(&a);
    rec_free(&b);
    return 0;
}
```

```text
ERROR: AddressSanitizer: attempting double-free on thread T0:
    #1 rec_free pe_bug.c:19
    #2 main pe_bug.c:28
    #3 __libc_start_call_main libc_start_call_main.h:58
    #4 __libc_start_main_impl ../csu/libc-start.c:360
ADDR is located 0 bytes inside of 4-byte region [ADDR,ADDR)
freed by thread T0 here:
    #1 rec_free pe_bug.c:19
    #2 main pe_bug.c:27
    #3 __libc_start_call_main libc_start_call_main.h:58
    #4 __libc_start_main_impl ../csu/libc-start.c:360
previously allocated by thread T0 here:
    #1 rec_new pe_bug.c:13
    #2 main pe_bug.c:23
    #3 __libc_start_call_main libc_start_call_main.h:58
    #4 __libc_start_main_impl ../csu/libc-start.c:360
SUMMARY: AddressSanitizer: double-free asan_malloc_linux.cpp:52 in free
```

The stderr output is ASan's report: `a` and `b` share one `name` buffer, the first `rec_free` frees it and the second is reported as a double free, with the allocating, freeing and failing frames named. The program's own `printf` output is not shown because the process aborts before the buffered line is flushed. The repair is either a deep copy function (`rec_clone`) or making the struct non-copyable by convention (return pointers only, so no one copies the struct by value). Problem E asks for the repair. A **destroy that sets the pointer to NULL** makes a second destroy of the same object harmless, at the cost of hiding a logic error.

Two more rules from experience: initialise every member in the constructor (the mutant that left the header uninitialised was caught by the sanitized test run; in general an uninitialised read is the job of MemorySanitizer or valgrind), and make the failure path the same code as the normal destroy where you can (`t_destroy` frees what is non-NULL), so there is one cleanup to get right.

---

## 9. Array of structs against structure of arrays

The layout of many small records decides the cache behaviour. **Array of structs (AoS)** keeps each record together: `P p[N]` with `x, y, z, vx, vy, vz, m, q`. **Structure of arrays (SoA)** keeps each field together: one array per field. I measured both on 4,194,304 particles of 8 floats (32 bytes each, 128 MB) in three workloads:

- **sum of one field** over all particles, 20 passes (a query that touches one member);
- **update**: `x += vx; y += vy; z += vz` for all particles, 10 passes (a streaming kernel that touches six of eight);
- **random access**: 20,000,000 particles chosen by a xorshift index, all 8 fields read for each (a lookup by id).

```text
T1 4194304 particles of 8 floats: checksums sum-x array of structs 668469320.0, structure of arrays 668469320.0; update all positions array of structs 115937333.2, structure of arrays 115937333.2; 20000000 random elements, all 8 fields each: array of structs 804163655.2, structure of arrays 804163655.2
```

| Workload | Array of structs (ms) | Structure of arrays (ms) |
|---|---|---|
| Sum of one field | 109 / 126 | 58 / 59 |
| Update positions | 98 / 158 | 58 / 69 |
| Random access, 8 fields | 225 / 311 | 1697 / 1662 |

The checksums are equal in both layouts, so both compute the same thing. The results:

- **Summing one field is about twice as fast in SoA** (59 ms against 109 and 126). In AoS each 64 byte cache line holds two particles, so reading one float of each still pulls all of memory through the cache: 128 MB per pass against 16 MB for the SoA array. The ratio of times is smaller than the ratio of traffic (8 to 1); I did not measure why, and I did not use hardware counters here. An earlier run of the same program on this machine gave 209 and 213 ms for AoS and 59 and 59 for SoA, so **the AoS timings vary by almost a factor of two between trials on a shared cloud machine** and only the direction is stable. The SoA sum is steady.
- **The update is 1.7 to 2.3 times faster in SoA**, again with noisy AoS times (98 against 158 ms). One possible explanation is that the SoA loops are simple unit stride loops the compiler can vectorise; I did not look at the assembly, so treat it as a hypothesis.
- **Random access reverses the result: AoS is 5 to 8 times faster** (225 and 311 ms against 1697 and 1662). A random particle in AoS lives in one or two cache lines, and the SoA version touches 8 different arrays, 8 separate cache misses for every particle.

So there is no best layout, only a layout for the access pattern: **SoA when loops touch a few fields of many records, AoS when code works on whole records** (lookup by key, passing a record to a function, records that are added and removed one at a time). Game engines, databases (columnar stores) and numeric libraries pick SoA; kernel objects (`task_struct`, `sk_buff`) are AoS because they are reached by pointer one at a time. A third option, **AoSoA** (arrays of small structs of arrays, e.g. 8 or 16 elements wide), keeps the vector friendliness of SoA with locality closer to AoS; I did not measure it. In the kernel, `____cacheline_aligned` annotations and field ordering by access frequency (hot fields in the first cache line, an idea behind the reorganisation tools; from memory) are the same concern at the level of a single struct.

---

## What it costs

| Pattern | Cost | Evidence |
|---|---|---|
| Struct passed by value, up to 64 bytes | No measurable difference from a pointer | T2: 16 and 64 bytes equal |
| Struct passed by value, 256 bytes | About 2x a pointer | T2 |
| Struct passed by value, 1024 bytes | About 12x a pointer (19 to 21 ns a call) | T2, with `noipa`; GCC otherwise removed the copy |
| Vtable call | One extra load and an indirect call; one pointer per object | `sizeof(Shape) = 8`; C 08 for timing |
| Opaque type | A heap allocation per object, no inlining across the boundary | Section 3 (reasoned, not timed) |
| Tagged union | Every node is as large as the largest alternative | `sizeof(Expr) = 24` |
| Intrusive list | No allocation per node, O(1) removal, but unlink from every list before free | Section 5 |
| Macro generated container | Code duplicated per type, harder error messages | Section 7 |
| SoA, one field sum | About 2x faster than AoS | T1 |
| SoA, random whole record | 5 to 8x slower than AoS | T1 |

---

## Common mistakes

1. Comparing structs with `memcmp` or hashing their bytes: padding makes equal values differ. Compare members.
2. Assuming struct assignment is a deep copy: pointer members are shared, and an owning struct double frees.
3. Passing large structs by value in hot paths. Use `const T *`.
4. Leaving `container_of` without the type check, or passing the wrong member name: the result is a wild pointer that points near the object. Use the checked macro.
5. Freeing an object that is still on a list (or deleting it from only one of its lists): the next walk reads freed memory.
6. Using `list_for_each_entry` while deleting. Use the `_safe` form.
7. Reading a tagged union member that is not the live one, or adding a kind and not updating a `switch` that has a `default:`.
8. Returning a pointer to a compound literal, or storing one past the end of its block.
9. Putting the base struct in the middle and casting the derived pointer to the base: the addresses differ. Base first, or `container_of`.
10. Calling a table entry that may be NULL without checking. Check in the dispatcher.
11. A constructor that leaks on a middle failure. One label per resource, released in reverse order, tested with a failing allocator.
12. Copying a struct with a flexible array member by assignment: only the fixed part moves.
13. Taking the address of a member of a `packed` struct and dereferencing it: an unaligned access.
14. Writing a struct to a file or a socket with `fwrite(&s, sizeof s, 1, f)`: padding, endianness and layout travel with it. Serialise field by field.

---

## Questions out loud

1. What does assigning one struct to another copy, and what does it not? Show a case where that is a bug.
2. Why can you not compare two structs with `memcmp`? What do you do instead?
3. Order these members to make the struct smallest: `char`, `double`, `char`, `int`, `short`. What is the size on x86-64?
4. Explain `container_of`. How does the macro check its arguments, and what does it need from the compiler?
5. What is the advantage of an intrusive list over a list of nodes that point at objects? What is its price?
6. How can one object sit on two lists at once? What must you do before freeing it?
7. Why does `list_del` poison the pointers? What does a crash at `0x100` tell you?
8. Draw the memory of a `Rect` made with the vtable pattern: the object, the table, the functions. What does a call to `shape_area(s)` do step by step?
9. How do you make an operation optional in an operations table, and who checks it?
10. Why is a struct pointer convertible to a pointer to its first member? What breaks if the base is the second member?
11. What does an opaque type buy a library, and what does it cost the user of the library?
12. A constructor allocates three things. Write the failure paths. How do you test them?
13. When is a tagged union better than a vtable? When worse?
14. You have an array of structs and a loop that reads one field of every record. What do you change, and what is the cost for other code?

---

## Problems

Five problems, in order of difficulty. **E (entry level)**, **M (medium)**, **H (hard)**.

**A (E).** Predict `sizeof` for each of these structs on x86-64 without running anything, then compile and compare.

```c
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include "../code/oop.h"

struct P1 { char a; int b; char c; };
struct P2 { int b; char a; char c; };
struct P3 { char name[5]; short s; long l; };
struct P4 { char a; struct { char x; long y; } in; char z; };

struct Node {
    int id;
    char tag;
    struct list_head link;
    double w;
};

int main(void)
{
    printf("P1 %zu P2 %zu P3 %zu P4 %zu\n", sizeof(struct P1), sizeof(struct P2), sizeof(struct P3), sizeof(struct P4));
    printf("Node: size %zu, offsetof(link) %zu\n", sizeof(struct Node), offsetof(struct Node, link));
    uintptr_t link_addr = 0x1010;
    printf("if &n->link is 0x%lx then n is 0x%lx\n", (unsigned long)link_addr, (unsigned long)(link_addr - offsetof(struct Node, link)));
    return 0;
}
```

**B (E).** In the same program, `struct Node` has a `list_head` member `link`. If the address of `n->link` is `0x1010`, what is the address of `n`? Use the offset that the program prints.

**C (M).** Using the `list_head` code from section 5, implement `list_len`, `list_move_tail(e, head)` (take an entry off whatever list it is on and append it to another) and `list_reverse(head)` in place. Write a main that checks them on a list of five, on moving two entries, and on an empty list.

**D (M).** Add overflow checking to the expression evaluator of section 4: `int x_eval_checked(const Expr *e, long *out)` returns 0 on success and -1 if any step overflows `long`. Cover the negation of `LONG_MIN` as well.

**E (H).** This program copies a struct that owns heap memory. Run it under AddressSanitizer, explain the report, and give a repaired version that cannot double free and whose destroy is safe to call twice.

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    int score;
} Rec;

static Rec rec_new(const char *name, int score)
{
    Rec r;
    r.name = malloc(strlen(name) + 1);
    strcpy(r.name, name);
    r.score = score;
    return r;
}

static void rec_free(Rec *r) { free(r->name); }

int main(void)
{
    Rec a = rec_new("ann", 5);
    Rec b = a;
    b.score = 6;
    printf("%s %d, %s %d\n", a.name, a.score, b.name, b.score);
    rec_free(&a);
    rec_free(&b);
    return 0;
}
```

**Where to solve them.** A and B need only a C compiler: save the file, compile with `gcc -std=gnu11 -Wall -Wextra`, and use `offsetof` and `sizeof`. C, D and E need the header code of sections 4 and 5, which you can paste into one file, and a test `main`. Compile C, D and E with `-g -fsanitize=address,undefined`. For more practice, the kernel's own list API in `include/linux/list.h` is the best reading: compare `list_add`, `list_del` and the iteration macros with the code above.

**How to approach them.** A: apply the rule (each member sits at a multiple of its alignment, the struct size is a multiple of the largest alignment) member by member and write the offsets down before running. For `P4` remember that a nested struct is aligned to its own largest member. B: `container_of` subtracts the offset of the member, so the answer is the address minus `offsetof(struct Node, link)`. C: `list_move_tail` is `list_del_init` followed by `list_add_tail`; `list_reverse` swaps `next` and `prev` of every node including the head, and you need a temporary. Test the empty list and the one-element list, where `next == prev == head`. D: use `__builtin_add_overflow` and `__builtin_mul_overflow`, check both children before combining, and remember that `-LONG_MIN` itself overflows. E: first state who owns `name`, then decide between making a deep copy and forbidding the copy.

---

## Answers

**A.** The sizes are 12, 8, 16 and 32. `P1` is `char`, pad 3, `int`, `char`, pad 3. Reordering to `P2` (the `int` first) saves 4 bytes. `P3` has a 5 byte array, 1 byte padding, a `short`, 8 bytes of padding for the `long`, and then the `long`: 5 + 1 + 2 = 8, then the `long` at offset 8, total 16. `P4` has `a` at 0, the inner struct (which has size 16 and alignment 8) at offset 8 to 24, `z` at 24, and padding to 32. The program printed:

```text
P1 12 P2 8 P3 16 P4 32
Node: size 32, offsetof(link) 8
if &n->link is 0x1010 then n is 0x1008
```

**B.** The program says `offsetof(link)` is 8 (the `int` at 0, the `char` at 4, padding to 8, then the 16 byte `list_head`, then the `double` at 24, for a total of 32 bytes). So `n` is `0x1010 - 8 = 0x1008`, as printed. This is exactly what `container_of(ptr, struct Node, link)` computes.

**C.** The solution follows. Reversal walks the ring once, swapping the two pointers of each node (the head included), and moving to the old `next`, which is the new `prev`:

```c
#include <stdio.h>
#include <stdlib.h>
#include "../code/oop.h"

typedef struct {
    int v;
    struct list_head link;
} Item;

static size_t list_len(const struct list_head *h)
{
    size_t n = 0;
    for (const struct list_head *p = h->next; p != h; p = p->next)
        n++;
    return n;
}

/* Move an entry from whatever list it is on to the tail of another. */
static void list_move_tail(struct list_head *e, struct list_head *head)
{
    list_del_init(e);
    list_add_tail(e, head);
}

/* Reverse in place by swapping the two pointers of every node, the head included. */
static void list_reverse(struct list_head *head)
{
    struct list_head *p = head;
    do {
        struct list_head *t = p->next;
        p->next = p->prev;
        p->prev = t;
        p = p->prev;   /* the old next */
    } while (p != head);
}

static void show(const char *label, struct list_head *h)
{
    Item *it;
    printf("%s (%zu):", label, list_len(h));
    list_for_each_entry(it, h, link)
        printf(" %d", it->v);
    printf("\n");
}

int main(void)
{
    struct list_head a, b;
    INIT_LIST_HEAD(&a);
    INIT_LIST_HEAD(&b);
    Item items[5];
    for (int i = 0; i < 5; ++i) {
        items[i].v = i + 1;
        list_add_tail(&items[i].link, &a);
    }
    show("a", &a);
    list_reverse(&a);
    show("a reversed", &a);
    list_move_tail(&items[2].link, &b);
    list_move_tail(&items[0].link, &b);
    show("a after moving 3 and 1", &a);
    show("b", &b);
    list_reverse(&b);
    show("b reversed", &b);
    struct list_head empty;
    INIT_LIST_HEAD(&empty);
    list_reverse(&empty);
    printf("empty list reversed, still empty: %s\n", list_empty(&empty) ? "yes" : "no");
    return 0;
}
```

```text
a (5): 1 2 3 4 5
a reversed (5): 5 4 3 2 1
a after moving 3 and 1 (3): 5 4 2
b (2): 3 1
b reversed (2): 1 3
empty list reversed, still empty: yes
```

`list_move_tail` calls `list_del_init` first so that the entry may be on another list, or on none. The empty list passes because `next == prev == head` and the swap changes nothing.

**D.** The evaluator checks both children before combining and uses the compiler builtins, which report overflow without relying on undefined signed overflow:

```c
#include <limits.h>
#include <stdio.h>
#include "../code/oop.h"

/* Evaluate with overflow detection: 0 on success, -1 if any step would overflow a long. */
static int x_eval_checked(const Expr *e, long *out)
{
    long a, b;
    switch (e->kind) {
    case X_NUM:
        *out = e->u.num;
        return 0;
    case X_ADD:
        if (x_eval_checked(e->u.bin.l, &a) || x_eval_checked(e->u.bin.r, &b))
            return -1;
        return __builtin_add_overflow(a, b, out) ? -1 : 0;
    case X_MUL:
        if (x_eval_checked(e->u.bin.l, &a) || x_eval_checked(e->u.bin.r, &b))
            return -1;
        return __builtin_mul_overflow(a, b, out) ? -1 : 0;
    case X_NEG:
        if (x_eval_checked(e->u.operand, &a))
            return -1;
        if (a == LONG_MIN)
            return -1;
        *out = -a;
        return 0;
    }
    return -1;
}

static void try(const char *label, Expr *e)
{
    long v = 0;
    int rc = x_eval_checked(e, &v);
    if (rc == 0)
        printf("%-22s -> %ld\n", label, v);
    else
        printf("%-22s -> overflow\n", label);
    x_free(e);
}

int main(void)
{
    try("(2 + 3) * (-4)", x_bin(X_MUL, x_bin(X_ADD, x_num(2), x_num(3)), x_neg(x_num(4))));
    try("LONG_MAX + 1", x_bin(X_ADD, x_num(LONG_MAX), x_num(1)));
    try("LONG_MAX + (-1)", x_bin(X_ADD, x_num(LONG_MAX), x_num(-1)));
    try("-(LONG_MIN)", x_neg(x_num(LONG_MIN)));
    try("(1 << 40) * (1 << 40)", x_bin(X_MUL, x_num(1L << 40), x_num(1L << 40)));
    try("deep: LONG_MAX + 1 * 0", x_bin(X_MUL, x_bin(X_ADD, x_num(LONG_MAX), x_num(1)), x_num(0)));
    return 0;
}
```

```text
(2 + 3) * (-4)         -> -20
LONG_MAX + 1           -> overflow
LONG_MAX + (-1)        -> 9223372036854775806
-(LONG_MIN)            -> overflow
(1 << 40) * (1 << 40)  -> overflow
deep: LONG_MAX + 1 * 0 -> overflow
```

The deep case shows that the overflow in the inner `LONG_MAX + 1` is reported even though the product is multiplied by zero, because the sum is computed before the product. Whether that is the behaviour you want is a design decision: a lazy evaluator would skip the left operand when the right is zero.

**E.** The report says "attempting double-free": `a` and `b` share the same `name` pointer after `Rec b = a`, so `rec_free(&a)` freed it and `rec_free(&b)` freed it again. The report names the allocating call in `rec_new`, the first free in `main`, and the second free that failed. The structure owns the buffer, so a copy must allocate its own. The repair gives the struct a deep copy `rec_clone`, and makes `rec_destroy` set the pointer to NULL so a second destroy is a harmless `free(NULL)`:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    int score;
} Rec;

static int rec_init(Rec *r, const char *name, int score)
{
    r->name = malloc(strlen(name) + 1);
    if (!r->name)
        return -1;
    strcpy(r->name, name);
    r->score = score;
    return 0;
}

/* A deep copy: the new record owns its own name. */
static int rec_clone(Rec *dst, const Rec *src) { return rec_init(dst, src->name, src->score); }

static void rec_destroy(Rec *r)
{
    free(r->name);
    r->name = NULL;
}

int main(void)
{
    Rec a, b;
    if (rec_init(&a, "ann", 5) != 0)
        return 1;
    if (rec_clone(&b, &a) != 0) {
        rec_destroy(&a);
        return 1;
    }
    b.score = 6;
    printf("%s %d, %s %d\n", a.name, a.score, b.name, b.score);
    printf("the names are different objects: %s\n", a.name != b.name ? "yes" : "no");
    rec_destroy(&a);
    rec_destroy(&b);
    rec_destroy(&b);
    printf("destroy twice is harmless: yes\n");
    return 0;
}
```

```text
ann 5, ann 6
the names are different objects: yes
destroy twice is harmless: yes
```

The alternative is to forbid copying: make `Rec` opaque (section 3), so callers only hold `Rec *` and cannot write `Rec b = a`.

---

## Cheat sheet

- Struct members are in declaration order with padding. Order members widest first. `sizeof` is a multiple of the largest alignment. Check layout with `offsetof` and `_Static_assert`.
- Copy, pass and return copy every member, arrays included; pointers are copied shallow. No `==`; `memcmp` fails on padding. Large structs go by `const T *`.
- Designated initialisers: `{.a = 1}` zeroes the rest. Compound literals live to the end of the enclosing block. Anonymous unions and structs: C11.
- Opaque type: declare `typedef struct T T;` in the header, define it in the `.c`. Pays a heap allocation and loses inlining, gains ABI stability and invariants.
- Tagged union: kind plus union, a `switch` on every read, no `default:`. Size is the largest alternative.
- Inheritance: embed the base as the first member for free upcasts; use `container_of(ptr, type, member)` otherwise. It needs GNU `typeof` and statement expressions for the type check.
- Intrusive list: the node lives in the object, removal is O(1), one object can be on many lists. Remove from all lists before freeing. Use the `_safe` iterator when deleting. Poison deleted nodes.
- Vtable: `static const` table per type, table pointer as the first member of each object, optional entries NULL and checked in the dispatcher. Reuse functions between tables for implementation inheritance.
- `sys/queue.h`: `TAILQ_ENTRY`, `TAILQ_HEAD`; `tqe_prev` points at the previous `tqe_next`. glibc has no `TAILQ_FOREACH_SAFE` (measured).
- Macro containers: one copy per type, type checked, harder to debug. `_Generic` is covered in C 10.
- Ownership: one owner per allocation, `_new`/`_free`, `const` for borrowers, deep copy by explicit function. Multi-resource constructor: one `goto` label per resource, reverse order, test with a failing allocator.
- Flexible array member: `sizeof` excludes it, allocate header plus data, never assign the struct. `packed`: smaller but unaligned member addresses.
- AoS for whole records and random access, SoA for streaming over a few fields; measure your own access pattern.

---

## Sources

All four links were fetched and checked for the content named.

- Statement expressions and `typeof`, used by `container_of` ([GCC manual, Statement Exprs](https://gcc.gnu.org/onlinedocs/gcc/Statement-Exprs.html)).
- Designated initialisers, including the GNU array range form ([GCC manual, Designated Inits](https://gcc.gnu.org/onlinedocs/gcc/Designated-Inits.html)).
- The BSD queue macros: `LIST`, `SLIST`, `STAILQ`, `TAILQ` ([queue(3)](https://man7.org/linux/man-pages/man3/queue.3.html)).
- The Linux kernel list API, the reference for the intrusive list in section 5 ([kernel documentation, linked lists](https://docs.kernel.org/core-api/list.html)).
- C 07, C 08 and the earlier tutorials in this track for the preprocessor, pointers, sanitizers and function pointers. C 10 (variadics, `_Generic`, `goto` cleanup in depth) and C 11 (secure C) continue from here.
