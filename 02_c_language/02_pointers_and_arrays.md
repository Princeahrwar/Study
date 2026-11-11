# C 02: Pointers and Arrays in Depth

*Part of the [C language plan](00_plan.md) · Time: about 4 hours (2.5 reading, 1.5 problems) · You need: day-to-day C, a compiler (`gcc` or `clang`), and [C 01](01_types_conversions_and_undefined_behavior.md) for undefined behaviour and conversions. There is no companion demo file: every program below was compiled and run, and its real output is printed under it*

Pointers are where C programs get their power and where most of their worst bugs live. Almost every confusing snippet in an interview ("what does this print", "why does this crash", "what is wrong with this declaration") comes down to a handful of rules: an array is **not** a pointer but turns into one almost everywhere; pointer arithmetic is scaled by the size of the pointed-to type; a parameter written as an array is really a pointer; a multi-dimensional array is one block laid out row by row; and a function has an address you can store, pass and call. This tutorial states each rule, shows it with a compiled program, measures what it costs, and ends with the three places people most often get it wrong: out-of-bounds access, dangling pointers and `void *` code with no type checking.

By the end you can:

- say what an expression's type is (`int *`, `int (*)[5]`, `int`) and which of them `a`, `&a`, `&a[0]`, `a + 1` and `&a + 1` have;
- list the **exceptions to array-to-pointer decay** and use them (`sizeof a / sizeof a[0]`);
- do pointer arithmetic and pointer differences correctly, stay inside the legal range, and write loops over a half-open range `[begin, end)`;
- explain why `void f(int a[5])` is `void f(int *a)`, and pass a 2D array, a pointer to an array and a length correctly;
- answer "`char *p = "abc"` against `char a[] = "abc"`" with sizes, storage and what happens on a write;
- lay out a 2D table three ways, say what each costs, and explain a 3 to 13 times difference between row-wise and column-wise access (measured);
- use a pointer to a pointer to change a caller's pointer (remove from a linked list without a special case for the head);
- read any declaration (`int (*(*pf)[2])(int)`), build a dispatch table, and write a `qsort` comparator that is not undefined;
- write generic code with `void *` and an element size, and say what it costs and gives up;
- recognise a dangling pointer and an out-of-bounds access, and say which tool catches which.

**Where the numbers come from.** Every program was compiled with GCC 13.3 (`-std=c11 -Wall -Wextra`) on an x86-64 Linux container (LP64: `int` 4 bytes, pointers 8) and run; the outputs are what it printed at the optimisation level named. The helper functions printed in the tutorial (the linked list, the pointer-range functions, the matrix functions, the generic functions, the comparator and the dispatch table) are the real source, tested against plain models: 6,219,416 checks passed under AddressSanitizer and UBSan, with the leak checker on. I tried 20 deliberate bugs in them (an off-by-one in a range, a missing free, a wrong step size, a vector that does not grow, a prefix match in the dispatch lookup, and others); all 20 were caught, 7 of them by the sanitizer, the leak checker or a timeout rather than by a wrong answer. Statements about ARM64, Clang, other C libraries and the exact wording of the standard are from memory or from the standard and are marked.

**Data and timing.** Machine: a cloud container, Intel Xeon 2.10 GHz, GCC 13.3.0, `-O2` for timings, one thread. Times are the best of 3 repetitions, shown as run 1 / run 2. Data is random from a seeded generator, so every run gives the same sums. Timings show the shape of the cost on this machine; the ratios will differ on yours.

---

## 1. What a pointer is

A **pointer** is a value that holds the address of an object, together with a **type** that says how to read the bytes there and how far one step goes. The type is the point: an `int *` and a `double *` can hold the same address and still behave differently, because `*p` reads 4 bytes in one case and 8 in the other, and `p + 1` moves by 4 or by 8.

Four rules cover most of what follows:

1. `&x` is the address of `x`, and has type "pointer to the type of `x`". `*p` is the object `p` points to. `*&x` is `x`.
2. A pointer is **valid** when it points to an object, to the element **one past the end** of an array (it may be formed and compared, but not dereferenced), or is null. Anything else is undefined behaviour the moment you form it, not only when you read through it.
3. A null pointer compares equal to 0 and must not be dereferenced. It does not have to be all zero bits (from memory: it is on every mainstream platform).
4. A pointer is not an integer. Casting to `uintptr_t` and back is the sanctioned way to treat one as a number, and the result of arithmetic on that number carries no promise.

## 2. Arrays, and what `a[i]` means

An **array** is a fixed number of objects of one type stored one after another with no gaps. `int a[5]` is five `int`s, 20 bytes here. The subscript is defined in terms of pointer arithmetic: `a[i]` means `*(a + i)`, nothing more. That is why `2[a]` is legal (addition commutes) and why `a[i]` with `i` out of range is a pointer past the object, not a "bounds error" the language knows about.

### 2.1 Decay and its exceptions

In most expressions an array **decays**: it is converted to a pointer to its first element. The standard lists three places where it does not (C11 6.3.2.1, from memory; C11 adds `_Alignof` as a fourth, which is rarely used):

1. as the operand of `sizeof`;
2. as the operand of unary `&`;
3. when it is a string literal used to initialise a character array.

Everywhere else, the name of an array is a pointer value. The program below prints the type of each expression using `_Generic`, then the sizes.

```c
#include <stdio.h>
#define TYPE_NAME(x) _Generic((x), \
    int: "int", int *: "int *", const int *: "const int *", \
    int (*)[5]: "int (*)[5]", int (*)[4]: "int (*)[4]", \
    char *: "char *", char (*)[4]: "char (*)[4]", size_t: "size_t", \
    default: "other")
int main(void) {
    int a[5] = {10, 20, 30, 40, 50};
    int *p = a;
    printf("a        has type %-12s (the array decayed to a pointer here)\n", TYPE_NAME(a));
    printf("&a[0]    has type %s\n", TYPE_NAME(&a[0]));
    printf("&a       has type %s\n", TYPE_NAME(&a));
    printf("*a       has type %s\n", TYPE_NAME(*a));
    printf("a + 1    has type %s\n", TYPE_NAME(a + 1));
    printf("&a + 1   has type %s\n", TYPE_NAME(&a + 1));
    printf("sizeof a = %zu, sizeof p = %zu, sizeof &a = %zu, sizeof *&a = %zu\n",
           sizeof a, sizeof p, sizeof &a, sizeof *&a);
    printf("elements: sizeof a / sizeof a[0] = %zu\n", sizeof a / sizeof a[0]);
    char s[] = "abc";
    printf("char s[] = \"abc\": sizeof s = %zu (the terminator counts), type of &s is %s\n",
           sizeof s, TYPE_NAME(&s));
    printf("a[2] = %d, *(a + 2) = %d, 2[a] = %d\n", a[2], *(a + 2), 2[a]);
    return 0;
}
```

```
a        has type int *        (the array decayed to a pointer here)
&a[0]    has type int *
&a       has type int (*)[5]
*a       has type int
a + 1    has type int *
&a + 1   has type int (*)[5]
sizeof a = 20, sizeof p = 8, sizeof &a = 8, sizeof *&a = 20
elements: sizeof a / sizeof a[0] = 5
char s[] = "abc": sizeof s = 4 (the terminator counts), type of &s is char (*)[4]
a[2] = 30, *(a + 2) = 30, 2[a] = 30
```

Read this output slowly, it is the most reused fact in the tutorial:

- `a` (decayed) and `&a[0]` have type `int *`: they point at the first element.
- `&a` has type `int (*)[5]`: a pointer to the **whole array**. Same address as `a`, but a step of `&a + 1` is 20 bytes, not 4 (section 3).
- `sizeof a` is 20 (the array), `sizeof p` is 8 (a pointer). `sizeof a / sizeof a[0]` is the element count, and works only where `a` is still an array, not where it came in as a parameter (section 4).
- A string literal initialising a `char` array keeps the terminator: `sizeof s` is 4 for `"abc"`.

| | Array `int a[5]` | Pointer `int *p` |
|---|---|---|
| What it is | 5 `int` objects | one pointer object |
| `sizeof` | 20 | 8 |
| Can be assigned (`a = ...`) | no | yes |
| `&` gives | `int (*)[5]` | `int **` |
| `a + 1`, `*a`, `a[i]` | work (after decay) | work |
| Storage of the elements | inside the array | somewhere else |

A classic trap (from memory, as told in *Expert C Programming*): a file defines `int a[100];` and another declares `extern int *a;`. Both compile and link, and the second reads the first elements as an address. Declare what you defined: `extern int a[];`.

## 3. Pointer arithmetic

Adding an integer `n` to a pointer to `T` moves it by `n * sizeof(T)` bytes. Subtracting two pointers into the same array gives the number of **elements** between them, as a `ptrdiff_t`. The program checks this in bytes, and shows what `&a + 1` does.

```c
#include <stddef.h>
#include <stdio.h>
int main(void) {
    int a[5] = {10, 20, 30, 40, 50};
    double d[3] = {1.5, 2.5, 3.5};
    int *p = a;
    printf("(char *)(p + 1) - (char *)p         = %td bytes (int is %zu bytes)\n",
           (char *)(p + 1) - (char *)p, sizeof(int));
    printf("(char *)(&d[1]) - (char *)&d[0]     = %td bytes (double is %zu bytes)\n",
           (char *)&d[1] - (char *)&d[0], sizeof(double));
    printf("(char *)(a + 1) - (char *)a         = %td\n", (char *)(a + 1) - (char *)a);
    printf("(char *)(&a + 1) - (char *)&a       = %td  (&a + 1 steps over the whole array)\n",
           (char *)(&a + 1) - (char *)&a);
    int *end = a + 5;                       /* one past the end: legal to form */
    printf("end - p = %td elements, p < end is %d\n", end - p, p < end);
    for (int *q = a; q != end; ++q) printf("%d ", *q);
    printf("\n&a[3] - &a[1] = %td, type ptrdiff_t, %zu bytes\n", &a[3] - &a[1], sizeof(ptrdiff_t));
    return 0;
}
```

```
(char *)(p + 1) - (char *)p         = 4 bytes (int is 4 bytes)
(char *)(&d[1]) - (char *)&d[0]     = 8 bytes (double is 8 bytes)
(char *)(a + 1) - (char *)a         = 4
(char *)(&a + 1) - (char *)&a       = 20  (&a + 1 steps over the whole array)
end - p = 5 elements, p < end is 1
10 20 30 40 50
&a[3] - &a[1] = 2, type ptrdiff_t, 8 bytes
```

The legal rules, which the compiler does not check:

- A pointer may point at any element or **one past the end**. `end - p`, `p < end` and `p != end` are fine for any two pointers into the same array (or one past it).
- Forming a pointer further than one past the end, or before the first element, is undefined behaviour, even if you never dereference it. Comparing or subtracting pointers into **different** objects is undefined too.
- Arithmetic on a null pointer is undefined (in C; from memory, C++ allows adding 0).
- `void *` has no size, so `p + 1` on a `void *` is not standard C. GCC treats it as a step of 1 (a GNU extension, warned by `-Wpointer-arith`).
- Dereferencing a pointer whose address is not a multiple of the type's alignment, or reading an object through a pointer of an unrelated type, is undefined (C 04 and C 01 section 5).

### 3.1 The half-open range

The cleanest way to write a loop over an array is a **half-open range** `[begin, end)`, the same convention as the C++ iterators: `end` is one past the last element, an empty range has `begin == end`, and the length is `end - begin`. The one-past-the-end rule exists for exactly this.

```c
/* Half-open ranges [begin, end), the way C++ iterators work. */
size_t count_equal(const int *begin, const int *end, int x) {
    size_t count = 0;
    for (const int *p = begin; p != end; ++p)
        if (*p == x) ++count;
    return count;
}

void reverse_range(int *begin, int *end) {
    if (begin == end) return;
    --end;                                         /* last element */
    while (begin < end) {
        int t = *begin;
        *begin++ = *end;
        *end-- = t;
    }
}

void rotate_left(int *begin, int *end, size_t k) {
    size_t n = (size_t)(end - begin);
    if (n == 0) return;
    k %= n;
    reverse_range(begin, begin + k);
    reverse_range(begin + k, end);
    reverse_range(begin, end);
}

/* First element not less than x in a sorted range, or end. */
const int *lower_bound_ptr(const int *begin, const int *end, int x) {
    while (begin < end) {
        const int *mid = begin + (end - begin) / 2;
        if (*mid < x) begin = mid + 1;
        else end = mid;
    }
    return begin;
}
```

A hand trace of `rotate_left([1 2 3 4 5], 2)` by three reversals (rotating left by `k` is: reverse the first `k`, reverse the rest, reverse everything). After reversing `[0, 2)` the array is `2 1 3 4 5`; after reversing `[2, 5)` it is `2 1 5 4 3`; after reversing all five it is `3 4 5 1 2`. The program that checks the paper problems (problem F) printed those lines. `lower_bound_ptr` is binary search on pointers: it returns `end` when every element is smaller, and the test checked it against a linear scan for every `x`.

## 4. Arrays as function parameters

An array cannot be passed to a function; its **address** is. In a parameter list, `int a[5]` is rewritten by the compiler to `int *a`, and the `5` is ignored (C11 6.7.6.3). The program shows what the callee sees, and what the compiler says about it.

```c
#include <stdio.h>
static size_t size_in_callee(int arr[5]) { return sizeof arr; }
static size_t size_in_callee_ptr(int *arr) { return sizeof arr; }
static int sum_ptr_to_array(int (*row)[5]) { int s = 0; for (int i = 0; i < 5; ++i) s += (*row)[i]; return s; }
static int sum_static(int n, const int arr[static 3]) { int s = 0; for (int i = 0; i < n; ++i) s += arr[i]; return s; }
int main(void) {
    int a[5] = {1, 2, 3, 4, 5};
    printf("in main:   sizeof a = %zu\n", sizeof a);
    printf("in callee: sizeof arr (declared int arr[5]) = %zu\n", size_in_callee(a));
    printf("in callee: sizeof arr (declared int *arr)   = %zu\n", size_in_callee_ptr(a));
    printf("pointer to the whole array: sum = %d\n", sum_ptr_to_array(&a));
    printf("static 3: sum of first 3 = %d\n", sum_static(3, a));
    return 0;
}
```

```
in main:   sizeof a = 20
in callee: sizeof arr (declared int arr[5]) = 8
in callee: sizeof arr (declared int *arr)   = 8
pointer to the whole array: sum = 15
static 3: sum of first 3 = 6
```

The compiler's warning for the first function was:

```
warning: 'sizeof' on array function parameter 'arr' will return size of 'int *' [-Wsizeof-array-argument]
```

So:

- `sizeof arr` inside the callee is the size of a pointer (8), whether the parameter was written `int arr[5]` or `int *arr`. The count `sizeof a / sizeof a[0]` is wrong there, and `-Wsizeof-array-argument` (in `-Wall`) warns. **Pass the length as a separate parameter.**
- To keep the size in the type, pass a **pointer to the array**: `int (*row)[5]`, called as `f(&a)`; inside, `(*row)[i]` is the element.
- `const int arr[static 3]` (C99) promises the caller passes at least 3 elements; compilers may warn when the argument is shorter (from memory: GCC does for visible arrays, and the promise is otherwise unchecked).
- `const int *` on a parameter says the function does not modify the elements; it lets callers pass both `const` and non-`const` arrays. Use it for every read-only array parameter.
- A function cannot return an array. It returns a pointer to storage that outlives the call (the caller's buffer, a `static`, or heap memory), never to a local (section 10).

## 5. Strings: `char *p` against `char a[]`

Both lines below mention `"abc"`, and mean different things. This is the first question in the plan's interview list.

```c
#include <stdio.h>
#include <string.h>
int main(void) {
    char *p = "abc";
    char a[] = "abc";
    printf("sizeof p = %zu, sizeof a = %zu, strlen(p) = %zu, strlen(a) = %zu\n",
           sizeof p, sizeof a, strlen(p), strlen(a));
    a[0] = 'x';
    printf("after a[0] = 'x': a = %s\n", a);
    char *q = "abc";
    printf("two equal literals are the same object here: %s\n", p == q ? "yes" : "no");
    return 0;
}
```

```
sizeof p = 8, sizeof a = 4, strlen(p) = 3, strlen(a) = 3
after a[0] = 'x': a = xbc
two equal literals are the same object here: yes
```

`char a[] = "abc"` is an **array** of 4 characters, initialised from the literal (exception 3 of section 2.1): the literal's bytes are copied into `a`, and writing to `a[0]` is fine. `char *p = "abc"` is a **pointer** to the string literal itself, which lives in read-only storage; the pointer is 8 bytes and writing through it is undefined. On this machine the write is a crash:

```c
#include <stdio.h>
int main(void) {
    char *p = "abc";
    printf("before\n");
    fflush(stdout);
    p[0] = 'x';              /* undefined behaviour: the literal is read-only here */
    printf("after: %s\n", p);
    return 0;
}
```

```
before
exit status 139
```

Exit status 139 is 128 plus signal 11, `SIGSEGV`. The standard only says the behaviour is undefined (and that two equal literals may or may not be the same object: the `p == q` line printed "yes" with GCC, which merges them). In your own code, declare literal pointers `const char *`, so the compiler rejects the write.

## 6. Multi-dimensional arrays

`int m[3][4]` is **an array of 3 arrays of 4 `int`s**, stored row after row ("row-major"): `m[i][j]` is at element `i * 4 + j` counted from the start. `m[i]` is itself an array of 4 and decays to `int *` pointing at that row. The type of `m` (decayed) is `int (*)[4]`, a pointer to a row, and `m + 1` moves by 16 bytes.

```c
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    int m[3][4] = {{0, 1, 2, 3}, {10, 11, 12, 13}, {20, 21, 22, 23}};
    int *flat = &m[0][0];
    printf("sizeof m = %zu, sizeof m[0] = %zu, sizeof m[0][0] = %zu\n", sizeof m, sizeof m[0], sizeof m[0][0]);
    printf("row-major: m[1][2] = %d, flat[1 * 4 + 2] = %d\n", m[1][2], flat[1 * 4 + 2]);
    printf("(char *)m[1] - (char *)m[0] = %td bytes (a row is 4 ints)\n", (char *)m[1] - (char *)m[0]);
    int (*row)[4] = m;                      /* pointer to a row of 4 ints */
    printf("row[2][1] = %d, (*(row + 2))[1] = %d, sizeof *row = %zu\n", row[2][1], (*(row + 2))[1], sizeof *row);

    /* three ways to get a 3 x 4 table from the heap */
    int (*t1)[4] = malloc(3 * sizeof *t1);              /* one block, real 2D array */
    int *t2 = malloc(3 * 4 * sizeof *t2);               /* one block, you index by hand */
    int **t3 = malloc(3 * sizeof *t3);                  /* array of row pointers */
    for (int i = 0; i < 3; ++i) t3[i] = malloc(4 * sizeof **t3);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 4; ++j) { t1[i][j] = i * 10 + j; t2[i * 4 + j] = i * 10 + j; t3[i][j] = i * 10 + j; }
    printf("t1[2][3] = %d, t2[2 * 4 + 3] = %d, t3[2][3] = %d\n", t1[2][3], t2[2 * 4 + 3], t3[2][3]);
    printf("sizeof *t1 = %zu (a whole row), sizeof *t3 = %zu (one pointer); the rows of t3 are separate allocations\n",
           sizeof *t1, sizeof *t3);
    for (int i = 0; i < 3; ++i) free(t3[i]);
    free(t3); free(t2); free(t1);
    return 0;
}
```

```
sizeof m = 48, sizeof m[0] = 16, sizeof m[0][0] = 4
row-major: m[1][2] = 12, flat[1 * 4 + 2] = 12
(char *)m[1] - (char *)m[0] = 16 bytes (a row is 4 ints)
row[2][1] = 21, (*(row + 2))[1] = 21, sizeof *row = 16
t1[2][3] = 23, t2[2 * 4 + 3] = 23, t3[2][3] = 23
sizeof *t1 = 16 (a whole row), sizeof *t3 = 8 (one pointer); the rows of t3 are separate allocations
```

Three ways to hold a `rows` by `cols` table in memory:

| Layout | Declaration | Allocations | Indexing | Notes |
|---|---|---|---|---|
| One block, real 2D array | `int (*t)[cols] = malloc(rows * sizeof *t)` | 1 | `t[i][j]` | needs `cols` known when declared (a variable-length array type); one `free` |
| One block, flat | `int *t = malloc(rows * cols * sizeof *t)` | 1 | `t[i * cols + j]` | works in any C and any language boundary; you write the arithmetic |
| Array of row pointers | `int **t`, one `malloc` per row | `rows + 1` | `t[i][j]` | rows may differ in length ("jagged"); one extra pointer load per access; many `free`s |

To pass a 2D array to a function, put the sizes **before** the array so that the parameter type can use them (a variably-modified parameter, C99; optional in C11, supported by GCC and Clang):

```c
/* A 2D array parameter: the sizes must come before the array. */
long long sum_by_rows(size_t rows, size_t cols, const int m[rows][cols]) {
    long long sum = 0;
    for (size_t i = 0; i < rows; ++i)
        for (size_t j = 0; j < cols; ++j)
            sum += m[i][j];                        /* consecutive addresses */
    return sum;
}

long long sum_by_cols(size_t rows, size_t cols, const int m[rows][cols]) {
    long long sum = 0;
    for (size_t j = 0; j < cols; ++j)
        for (size_t i = 0; i < rows; ++i)
            sum += m[i][j];                        /* jumps cols * 4 bytes */
    return sum;
}

void transpose(size_t rows, size_t cols, const int in[rows][cols], int out[cols][rows]) {
    for (size_t i = 0; i < rows; ++i)
        for (size_t j = 0; j < cols; ++j)
            out[j][i] = in[i][j];
}
```

Both sum functions touch the same elements. The first walks addresses that are 4 bytes apart; the second jumps `cols * 4` bytes on every step. I timed them on a 256 by 256 table (256 KB, repeated 256 times so the total work matches) and a 4096 by 4096 table (64 MB), and on the jagged layout.

```
T1 256 x 256 ints (256 KB): sum by rows 32599019, by columns 32599019, jagged rows 32599019; all equal: yes
T1 4096 x 4096 ints (65536 KB): sum by rows 8380003184, by columns 8380003184, jagged rows 8380003184; all equal: yes
```

| Job | Time (ms), run 1 / run 2 |
|---|---|
| 256 x 256, by rows | 5.3 / 5.0 |
| 256 x 256, by columns | 16.5 / 16.2 |
| 256 x 256, jagged rows, by rows | 5.2 / 5.1 |
| 4096 x 4096, by rows | 14.0 / 12.5 |
| 4096 x 4096, by columns | 154.9 / 158.0 |
| 4096 x 4096, jagged rows, by rows | 15.6 / 14.4 |

Column-wise access was about 3 times slower on the small table and about 11 to 13 times slower on the large one. The arithmetic is identical; the difference is the memory: row-wise access uses every byte of each cache line it loads, column-wise access uses 4 bytes of each 64-byte line and, on the large table, comes back to the line only after it has been evicted. **Order the loops so that the last index changes fastest.** The jagged layout, read row by row, matched the contiguous one here, because each row is itself contiguous; its costs are the extra allocations and the extra pointer load, not the traversal.

## 7. Pointers to pointers

A `T **` is a pointer to a pointer to `T`. Three uses cover nearly everything:

1. **Changing a caller's pointer.** C passes by value, so a function that must change a pointer (a list head, a buffer it allocates) takes its address: `Node **head`.
2. **An array of pointers**, such as `char *argv[]` (a `char **` after decay) or the jagged table above.
3. **A cursor that walks a chain of links**, which is the idea in the next example.

Removing a node from a singly linked list is the textbook use of the first and third. The usual version keeps a `prev` pointer and needs a special case for the head. With a `Node **`, the cursor always points at **the pointer that points at the current node**: first the caller's `head` variable, then some node's `next` field. Removing the node is then one assignment to `*pp`, wherever it is.

```c
typedef struct Node {
    int value;
    struct Node *next;
} Node;

Node *list_push(Node *head, int value) {          /* new head */
    Node *n = malloc(sizeof *n);
    n->value = value;
    n->next = head;
    return n;
}

/* Remove the first node holding value. pp always points at the pointer
   that points at the current node: the head variable, then some next field. */
void list_remove_first(Node **head, int value) {
    Node **pp = head;
    while (*pp && (*pp)->value != value)
        pp = &(*pp)->next;
    if (*pp) {
        Node *dead = *pp;
        *pp = dead->next;                          /* works for the head too */
        free(dead);
    }
}

/* The same job with a previous pointer: the head needs its own case. */
Node *list_remove_first_prev(Node *head, int value) {
    Node *prev = NULL, *cur = head;
    while (cur && cur->value != value) {
        prev = cur;
        cur = cur->next;
    }
    if (!cur) return head;
    if (prev) prev->next = cur->next;
    else head = cur->next;
    free(cur);
    return head;
}

void list_insert_sorted(Node **head, int value) {
    Node **pp = head;
    while (*pp && (*pp)->value < value)
        pp = &(*pp)->next;
    Node *n = malloc(sizeof *n);
    n->value = value;
    n->next = *pp;
    *pp = n;
}

void list_free(Node **head) {
    while (*head) {
        Node *dead = *head;
        *head = dead->next;
        free(dead);
    }
}
```

A trace on the list `1 -> 2 -> 3`. `list_remove_first(&head, 1)`: `pp` starts at `&head`, `(*pp)->value` is 1, so the loop does not run; `*pp = dead->next` makes `head` point at 2. Then for 3 on the list `2 -> 3`: `pp` moves to `&node2->next`, where `*pp` is node 3; `*pp = dead->next` writes null into node 2's `next`. No case distinguishes the two. For an absent value, `*pp` ends as null and nothing is freed. Problem G prints these three cases. `list_remove_first_prev` is the same job with a `prev` pointer; the tests ran both against an array model for random lists, and the second one needs the `if (prev) ... else head = ...` split that the first does not.

## 8. Function pointers

A function has an address, and a **pointer to function** stores it. The name of a function decays to a pointer to it, like an array name, so `fp = twice` and `fp = &twice` mean the same, and `fp(4)` and `(*fp)(4)` both call it.

### 8.1 Reading a declaration

Start at the **name**, then go right while you can (`[]` array, `()` function), then left (`*` pointer to, a type), and treat parentheses as a restart. `int *a[3]` reads "a is an array of 3 pointers to int". `int (*a)[3]` reads "a is a pointer to an array of 3 ints". `int *(*f)(int)` reads "f is a pointer to a function taking int that returns a pointer to int". Two `typedef`s make the hard ones readable, and the program shows them in use:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef void (*Handler)(int);                  /* a pointer to a function taking int, returning void */
static Handler install(int signo, Handler h) { printf("install(%d)\n", signo); return h; }
/* the same thing without the typedef, as it appears in the C standard's signal() */
static void (*install2(int signo, void (*h)(int)))(int) { return install(signo, h); }
static void on_alarm(int n) { printf("alarm handler called with %d\n", n); }
static int twice(int x) { return 2 * x; }
static int square(int x) { return x * x; }
static int apply_all(const int *v, size_t n, int (*f)(int)) { int s = 0; for (size_t i = 0; i < n; ++i) s += f(v[i]); return s; }
struct Command { const char *name; int (*fn)(int); };
int main(void) {
    int (*fp)(int) = twice;                    /* the name of a function decays like an array */
    int (*gp)(int) = &twice;                   /* with or without the & */
    printf("fp(4) = %d, (*fp)(4) = %d, fp == gp is %d\n", fp(4), (*fp)(4), fp == gp);
    int v[3] = {1, 2, 3};
    printf("apply_all twice = %d, square = %d\n", apply_all(v, 3, twice), apply_all(v, 3, square));
    Handler old = install2(14, on_alarm);
    old(14);
    struct Command table[] = {{"twice", twice}, {"square", square}};
    for (size_t i = 0; i < sizeof table / sizeof table[0]; ++i)
        printf("%s(7) = %d\n", table[i].name, table[i].fn(7));
    int (*arr_of_fp[2])(int) = {twice, square};            /* an array of 2 pointers to function */
    int (*(*pf)[2])(int) = &arr_of_fp;                       /* a pointer to that array */
    printf("(*pf)[1](5) = %d, sizeof fp = %zu\n", (*pf)[1](5), sizeof fp);
    return 0;
}
```

```
fp(4) = 8, (*fp)(4) = 8, fp == gp is 1
apply_all twice = 12, square = 14
install(14)
alarm handler called with 14
twice(7) = 14
square(7) = 49
(*pf)[1](5) = 25, sizeof fp = 8
```

`install2` is the declaration of the standard `signal` function without a typedef: a function taking `(int, void (*)(int))` and returning a pointer to a function taking `int`. With `typedef void (*Handler)(int);` it becomes `Handler install(int, Handler)`. **Use a typedef for every function-pointer type that appears more than once.** The array of function pointers `arr_of_fp` and the pointer to it `pf` show how fast declarations grow without one.

### 8.2 Dispatch tables and callbacks

A table of `{name, function}` replaces a long `if`/`switch` and lets data choose the code. The tested version:

```c
typedef int (*BinOp)(int, int);

static int op_add(int a, int b) { return a + b; }
static int op_sub(int a, int b) { return a - b; }
static int op_mul(int a, int b) { return a * b; }
static int op_max(int a, int b) { return a > b ? a : b; }

typedef struct {
    const char *name;
    BinOp fn;
} OpEntry;

static const OpEntry OPS[] = {
    {"add", op_add}, {"sub", op_sub}, {"mul", op_mul}, {"max", op_max},
};

BinOp find_op(const char *name) {
    for (size_t i = 0; i < sizeof OPS / sizeof OPS[0]; ++i)
        if (strcmp(OPS[i].name, name) == 0) return OPS[i].fn;
    return NULL;
}
```

Callbacks follow the same shape: the library calls a function the caller supplied. `qsort`, `bsearch` and signal handlers are callbacks. A callback usually needs state, so APIs add a `void *context` parameter that the library hands back unchanged (the caller casts it back to its real type).

### 8.3 A comparator that looks right

`qsort` wants a comparator returning negative, zero or positive. Writing it as `a - b` is common and wrong: the subtraction can overflow (C 01 section 4).

```c
/* Looks fine, and is wrong: a - b can overflow. */
int cmp_int_subtract(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

int cmp_int(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}
```

```c
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
static int cmp_sub(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
static int cmp_ok(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}
int main(void) {
    int v[2] = {INT_MIN, 1};
    printf("cmp_sub(INT_MIN, 1) = %d, cmp_sub(1, INT_MIN) = %d\n", cmp_sub(&v[0], &v[1]), cmp_sub(&v[1], &v[0]));
    qsort(v, 2, sizeof v[0], cmp_sub);
    printf("qsort with a - b:           %d %d\n", v[0], v[1]);
    int w[2] = {INT_MIN, 1};
    qsort(w, 2, sizeof w[0], cmp_ok);
    printf("qsort with (x > y)-(x < y): %d %d\n", w[0], w[1]);
    return 0;
}
```

At `-O0`:

```
cmp_sub(INT_MIN, 1) = 2147483647, cmp_sub(1, INT_MIN) = -2147483647
qsort with a - b:           1 -2147483648
qsort with (x > y)-(x < y): -2147483648 1
```

At `-O2` the output was identical:

```
cmp_sub(INT_MIN, 1) = 2147483647, cmp_sub(1, INT_MIN) = -2147483647
qsort with a - b:           1 -2147483648
qsort with (x > y)-(x < y): -2147483648 1
```

`qsort` with `a - b` put `INT_MIN` **after** 1, because `INT_MIN - 1` wrapped to a large positive number; the version with `(x > y) - (x < y)` sorted correctly. Both builds agreed here, but only the second is defined: with `-fsanitize=undefined` the first run reports

```
runtime error: signed integer overflow: 1 - -2147483648 cannot be represented in type 'int'
```

Comparing with `>` and `<` also cannot overflow for any pair of values of any type, including `long long` and `double`.

### 8.4 What a call through a pointer costs

When the compiler can see which function the pointer holds, it can inline the call. When it cannot, every call is an indirect call and the loop cannot be vectorised. I summed `i & 3` over 200 million iterations directly and through a function pointer obtained at run time:

```
T2 200000000 iterations: direct sum 300000000, sum through a function pointer 300000000; equal: yes
```

| Job | Time (ms), run 1 / run 2 |
|---|---|
| 200 million additions, direct | 54.6 / 49.1 |
| 200 million calls through a function pointer | 232.4 / 242.7 |

About 1.2 ns a call against about 0.25 ns for the inlined, vectorised loop: roughly 4 to 5 times. For most programs this is irrelevant; in a hot inner loop it is why `qsort` with a callback is slower than a sort the compiler has specialised for the type (from memory: the C++ `std::sort` inlines its comparison, which is a common explanation for it beating `qsort`; not measured here).

Pointers to functions and pointers to objects are different kinds of pointer. Converting one to the other is not defined by the C standard; POSIX requires it to work for `dlsym` (from memory), which is why you see it there.

## 9. `void *` and generic code

`void *` is a pointer to an object of unknown type. Any object pointer converts to `void *` and back without a cast and without loss; you cannot dereference it or do arithmetic on it (section 3), so generic code takes an **element size** and does its own byte arithmetic through an `unsigned char *`, the one pointer type allowed to look at the bytes of any object.

```c
void swap_bytes(void *a, void *b, size_t n) {
    unsigned char *x = a, *y = b;
    while (n--) {
        unsigned char t = *x;
        *x++ = *y;
        *y++ = t;
    }
}

/* Linear search over n items of `size` bytes each. */
void *find_first(const void *base, size_t n, size_t size, const void *key,
                 int (*equal)(const void *, const void *)) {
    const unsigned char *p = base;
    for (size_t i = 0; i < n; ++i, p += size)
        if (equal(p, key)) return (void *)p;
    return NULL;
}

void reverse_items(void *base, size_t n, size_t size) {
    if (n < 2) return;
    unsigned char *lo = base, *hi = lo + (n - 1) * size;
    while (lo < hi) {
        swap_bytes(lo, hi, size);
        lo += size;
        hi -= size;
    }
}

void insertion_sort(void *base, size_t n, size_t size,
                    int (*cmp)(const void *, const void *)) {
    unsigned char *p = base;
    for (size_t i = 1; i < n; ++i)
        for (size_t j = i; j > 0 && cmp(p + (j - 1) * size, p + j * size) > 0; --j)
            swap_bytes(p + (j - 1) * size, p + j * size, size);
}

typedef struct {
    void *data;
    size_t size, len, cap;                         /* item size, items, room */
} Vec;

int vec_init(Vec *v, size_t size) {
    v->data = NULL;
    v->size = size;
    v->len = v->cap = 0;
    return 0;
}

int vec_push(Vec *v, const void *item) {
    if (v->len == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 4;
        void *bigger = realloc(v->data, cap * v->size);
        if (!bigger) return -1;
        v->data = bigger;
        v->cap = cap;
    }
    memcpy((unsigned char *)v->data + v->len * v->size, item, v->size);
    ++v->len;
    return 0;
}

void *vec_at(const Vec *v, size_t i) {
    return i < v->len ? (unsigned char *)v->data + i * v->size : NULL;
}

void vec_free(Vec *v) {
    free(v->data);
    v->data = NULL;
    v->len = v->cap = 0;
}
```

How it works: `swap_bytes` swaps two objects of `n` bytes one byte at a time; `find_first` steps through the array in strides of `size` and asks a callback whether an element equals the key; `reverse_items` and `insertion_sort` call `swap_bytes` on elements `size` bytes apart; `Vec` stores any item type in one growing block, doubling its capacity (from 4) with `realloc`, and `vec_at` returns a pointer to element `i` or null. The tests ran each against typed oracles (ints, a struct with a double, `qsort`), and checked that the capacity doubles exactly.

Compared with typed code you give up two things:

1. **Type checking.** `find_first(a, n, sizeof(int), &key, eq_int)` compiles just as happily with `sizeof(long)`, or with `&key` pointing at a `double`. Nothing checks that `size`, the pointer types and the callback agree. This is why generic C code is written once, tested hard, and wrapped in small typed functions or macros.
2. **Speed**, a little. The byte-wise swap cannot use one register move per element. I reversed 20 million `int`s both ways:

```
T3 reversing 20000000 ints: generic reverse_items and typed reverse_range give the same array: yes
```

| Job | Time (ms), run 1 / run 2 |
|---|---|
| `reverse_items`, generic, 20 million ints | 32.8 / 30.0 |
| `reverse_range`, typed, 20 million ints | 6.0 / 5.2 |

The generic version was about 5 to 6 times slower. A real generic library speeds this up by swapping 8 bytes at a time when the size allows it, or by using `memcpy` with a fixed small buffer (from memory of how common `qsort` implementations work; not measured here).

Alignment matters in one more place: `malloc` returns storage suitably aligned for any type, but `char buf[100]` is only aligned for `char`. Putting an `int` or a struct at an arbitrary offset in a byte buffer needs `memcpy` or an aligned buffer (C 04).

## 10. When it goes wrong: dangling pointers and out-of-bounds access

**A dangling pointer** points at an object whose lifetime has ended: a local after its function returned, freed heap memory, a `realloc`ed block. Using it is undefined, and the common symptom is "it works" at one optimisation level and fails at another:

```c
#include <stdio.h>
static int *escaped;                              /* a pointer that outlives the call */
static void remember(void) {
    int x = 42;
    escaped = &x;                                 /* x dies when remember() returns */
}
static int clobber(int a, int b) {                /* reuses the same stack space */
    int t[4] = {a, b, a + b, a * b};
    return t[2] + t[3];
}
int main(void) {
    remember();
    printf("right after the call: *escaped = %d\n", *escaped);
    int junk = clobber(7, 9);
    printf("after another call (%d):  *escaped = %d\n", junk, *escaped);
    return 0;
}
```

GCC 13 warns about the first function:

```
warning: storing the address of local variable 'x' in 'escaped' [-Wdangling-pointer=]
```

At `-O0` the program prints:

```
right after the call: *escaped = 42
after another call (79):  *escaped = 42
```

At `-O2`:

```
right after the call: *escaped = 32767
after another call (79):  *escaped = 32767
```

At `-O0` the dead variable's stack slot still held 42 and nothing overwrote it in this run, so the program "worked". At `-O2` the compiler treated the dead variable as uninitialised (its own second warning says so) and the read returned stack garbage; the exact value depends on what the stack held and is not something to rely on. This is the answer to "a function returns a pointer to a local array; why does it sometimes work?": the memory has not been reused **yet**, and nothing in the language says when it will be. With AddressSanitizer (and `ASAN_OPTIONS=detect_stack_use_after_return=1` for the return case) the first bad read stops the program:

```
ERROR: AddressSanitizer: stack-use-after-scope
READ of size 4 thread T0
```

**An out-of-bounds access** reads or writes past the object. The standard gives no check, so the program reads whatever is next, and the optimiser may assume it never happens (C 01 section 6):

```c
#include <stdio.h>
int main(void) {
    int a[5] = {1, 2, 3, 4, 5};
    int i = 5;
    printf("a[5] = %d\n", a[i]);                /* one past the end: reading it is undefined */
    return 0;
}
```

Under AddressSanitizer:

```
ERROR: AddressSanitizer: stack-buffer-overflow
READ of size 4 thread T0
```

Tools: `-Wall -Wextra` for what the compiler can see; AddressSanitizer for stack, heap and global overflows, use-after-free and use-after-scope; UBSan for misaligned access, bad shifts and overflow; Valgrind for the same class of bugs without recompiling (C 03). None of them replaces tests that run the bad path.

## 11. `const` with pointers, in one table

Read the declaration right to left, as in section 8.1: `const` applies to what is on its **left**, or to what is on its right if nothing is on its left.

| Declaration | Reads | Can change `*p` | Can change `p` |
|---|---|---|---|
| `int *p` | pointer to int | yes | yes |
| `const int *p` (or `int const *p`) | pointer to const int | no | yes |
| `int *const p` | const pointer to int | yes | no |
| `const int *const p` | const pointer to const int | no | no |

Use `const T *` for every pointer parameter the function only reads. Converting `T *` to `const T *` is allowed implicitly; the other direction needs a cast and is a smell. More on `const`, `volatile` and `restrict` in C 05.

## 12. Complexities and costs

| Operation | Cost | Note |
|---|---|---|
| `a[i]`, `*p`, `p + n` | `O(1)` | scaled by `sizeof(T)` |
| pointer difference | `O(1)` | elements, not bytes |
| `list_remove_first` | `O(n)` | one assignment for the unlink itself |
| `lower_bound_ptr` | `O(log n)` | needs sorted data |
| `rotate_left` by three reversals | `O(n)` time, `O(1)` memory | |
| `vec_push` | `O(1)` amortised | doubles when full; invalidates pointers into the old block |
| `swap_bytes` | `O(size)` | byte at a time here |
| call through a function pointer | `O(1)` | about 1.2 ns here, 4 to 5 times an inlined add |
| 2D traversal | `O(rows * cols)` | order of loops changes the constant by up to 13 times here |

## 13. Common mistakes

1. Using `sizeof a / sizeof a[0]` on a **parameter** (it is a pointer): the answer is 2 on a 64-bit machine with `int` elements.
2. Writing through a `char *` that points at a string literal.
3. Treating `&a` and `a` as the same: they have the same address and different types, so `&a + 1` and `a + 1` differ.
4. Forming a pointer two or more elements past the end of an array, or comparing pointers into different arrays.
5. A column-first loop over a row-major table.
6. Returning, or storing in a longer-lived place, a pointer to a local variable; keeping a pointer into a `Vec` across a push (it may have been moved by `realloc`).
7. `qsort` comparators that subtract, or return a `bool` instead of a three-way result.
8. Passing a `T **` where a 2D array is expected (`int **` is not `int (*)[N]`).
9. `void *` code with the wrong element size, or a callback for a different type; the compiler will not tell you.
10. Declaring `extern int *a;` for something defined as `int a[N];`.
11. Casting away `const` and then writing.
12. Reading a complicated declaration from the left instead of from the name.

## 14. Questions to answer out loud

1. What are the exceptions to array-to-pointer decay? Give an example of each.
2. What are the types and sizes of `a`, `&a`, `&a[0]`, `a + 1` and `&a + 1` for `int a[5]`?
3. Why does `2[a]` compile? What does `a[i]` mean?
4. What does `sizeof` give for an array parameter, and what do you do about it?
5. What is the difference between `char *p = "abc"` and `char a[] = "abc"`? What happens on `p[0] = 'x'`?
6. Where may a pointer legally point, and why is "one past the end" allowed?
7. How is `m[i][j]` found in memory for `int m[3][4]`? Why is column-first access slower, and by about how much?
8. Name three ways to hold a 2D table on the heap, and one cost of each.
9. Why does a function that changes a list head take a `Node **`? How does it avoid a special case for the head?
10. Read `int (*(*pf)[2])(int)` aloud. How does a `typedef` help?
11. Why is `return a - b;` a bad `qsort` comparator?
12. How does generic code in C know the size of its elements? What does it give up?
13. A function returns a pointer to a local array and the program seems to work. Why, and what do you tell the author?
14. What is the difference between `const int *p`, `int *const p` and `const int *const p`?

## 15. Problems

### 15.1 On paper

Answer first, then run each one. Assume GCC on x86-64 Linux, `int` is 4 bytes, pointers 8.

**A.** `int a[5];` What are `sizeof(a)`, `sizeof(&a)`, `sizeof(a + 0)`, `&a[4] - &a[0]` and `(char *)(&a + 1) - (char *)a`?

**B.** `int b[] = {1, 2, 3, 4}; int *p = b; p += 2;` What do `*p`, `p[-1]`, `-1[p]` and `p - b` give? (Mind the precedence of the last but one.)

**C.** `int m[3][4]` with `m[i][j] = 10 * i + j`. What do `*(*(m + 1) + 2)`, `**m`, `sizeof m[1]`, `(m + 2) - m`, `m[2] - m[0]` and `*m[2]` give? Why do `(m + 2) - m` and `m[2] - m[0]` differ?

**D.** Read each declaration aloud, and give its `sizeof`: `int *d1[3]`, `int (*d2)[3]`, `int *(*d3)(int)`, `const int *d4`, `int *const d5`, `char (*(*d6[3])(void))[5]`. What is `sizeof *d2`?

**E.** `void f(int a[10]) { printf("%zu", sizeof a); }` called as `f(big)` with `int big[10]`. What does it print, and what is `sizeof big` in the caller?

**F.** Trace `rotate_left` on `[1 2 3 4 5]` with `k = 2` as three reversals of half-open ranges, and say which range each call reverses.

**G.** A list `1 -> 2 -> 3`, with `list_remove_first(&head, v)` called for `v = 1`, then `3`, then `9`. Draw what `pp` points at on each step, and the list after each call.

### 15.2 On LeetCode

Solve these in C. For each, say before coding where the pointers start, where they end, and what stays valid. Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | What to practise |
|---|---|---|
| Reverse Linked List | 206 | A `Node **` or three pointers; section 7 |
| Remove Nth Node From End of List | 19 | Two cursors; the `Node **` trick for the head |
| Remove Duplicates from Sorted Array | 26 | Read and write pointers into one array |
| Rotate Array | 189 | Three reversals (section 3.1) |
| Merge Sorted Array | 88 | Fill from the back so nothing is overwritten |
| Rotate Image | 48 | 2D indexing, and the transpose idea of section 6 |
| Spiral Matrix | 54 | Four moving bounds on a 2D array |
| Search a 2D Matrix | 74 | `lower_bound_ptr` on the flattened view `t[i * cols + j]` |
| Reverse String | 344 | Two pointers; no extra array |

### 15.3 In your editor

**A.** Write `Node *list_reverse(Node *head)` twice, once with three pointers and once with a `Node **`, and test both on random lists against an array model, under AddressSanitizer.

**B.** Write a generic `void *bsearch_first(const void *key, const void *base, size_t n, size_t size, int (*cmp)(const void *, const void *))` that returns the **first** match, and test it against a linear scan on arrays with duplicates.

**C.** Write a generic quicksort or heapsort with the `qsort` signature, test it against `qsort` on `int`s, `double`s and a struct, and time it against `qsort` on 10 million `int`s. Then add a typed `int` version and see how much the callback costs on your machine.

**D.** Use `_Generic` (as in section 2.1) to print the type of ten expressions of your own, and check each against your prediction before you run it.

**E.** Make `sum_by_cols` faster without changing its result by processing the table in 64 by 64 tiles, and time it against both versions in section 6 on a 4096 by 4096 table.

**F.** Add `vec_pop`, `vec_insert(i)` and `vec_erase(i)` to `Vec` (use `memmove`; C 07), and test with AddressSanitizer on random sequences against an array model. What must you do to a pointer you got from `vec_at` before an insert?

**G.** Build a state machine for a tiny protocol parser with a table of `{state, handler}` function pointers, and test it against a switch-based version on random inputs.

### Answers

The program that checks the paper problems printed:

```
A sizeof(a) = 20, sizeof(&a) = 8, sizeof(a + 0) = 8, &a[4] - &a[0] = 4, (char *)(&a + 1) - (char *)a = 20
B *p = 3, p[-1] = 2, -1[p] = -4, p - b = 2
C *(*(m + 1) + 2) = 12, **m = 0, sizeof m[1] = 16, (m + 2) - m = 2, m[2] - m[0] = 8, *m[2] = 20
D sizeof d1 = 24, d2 = 8, d3 = 8, d4 = 8, d5 = 8, d6 = 24, *d2 = 12
E f(big) prints 8, sizeof big in the caller is 40
F rotate_left([1 2 3 4 5], 2): after reversing [0,2): 2 1 3 4 5 | after reversing [2,5): 2 1 5 4 3 | after reversing all: 3 4 5 1 2
G after removing 1 (the head): 2 3 | after removing 3 (the tail): 2 | after removing 9 (absent): 2
```

**A.** 20, 8, 8, 4 and 20. `sizeof(a)` is the array (exception 1); `&a` is a pointer; `a + 0` has decayed to a pointer; the difference of two `int *` is in elements; `&a + 1` steps over the whole array, 20 bytes.

**B.** 3, 2, -4 and 2. `-1[p]` parses as `-(1[p])`, which is `-p[1]`, so the value is `-4`; `p - b` is 2 elements.

**C.** 12, 0, 16, 2, 8 and 20. `m + 2` and `m` have type `int (*)[4]`, so their difference counts rows (2); `m[2]` and `m[0]` are arrays that decay to `int *`, so their difference counts ints (8). `*m[2]` is `m[2][0]`, which is 20.

**D.** `d1` is an array of 3 pointers to int (24 bytes). `d2` is a pointer to an array of 3 ints (8; `*d2` is the array, 12). `d3` is a pointer to a function taking int and returning a pointer to int (8). `d4` is a pointer to a const int (8). `d5` is a const pointer to int (8). `d6` is an array of 3 pointers to functions taking nothing and returning a pointer to an array of 5 chars (24).

**E.** It prints 8: the parameter is `int *`, and the 10 is ignored. In the caller, `sizeof big` is 40. GCC warned about this with `-Wsizeof-array-argument`.

**F.** `reverse_range(begin, begin + 2)` turns `1 2 3 4 5` into `2 1 3 4 5`; `reverse_range(begin + 2, end)` turns it into `2 1 5 4 3`; `reverse_range(begin, end)` gives `3 4 5 1 2`.

**G.** Removing 1: `pp` stays at `&head`, `*pp` is node 1, `*pp = node1->next`: the list is `2 -> 3`. Removing 3: `pp` moves to `&node2->next`, `*pp` is node 3, `*pp = NULL`: the list is `2`. Removing 9: `pp` walks to the `next` field of node 2, where `*pp` is null; the loop stops and nothing is freed: the list is still `2`. The program printed `2 3`, `2` and `2`.

## Cheat sheet

- **A pointer is an address plus a type.** The type sets the stride and the width of `*p`.
- **`a[i]` is `*(a + i)`.** An array name decays to a pointer to its first element, except under `sizeof`, under `&`, and as a string-literal initialiser of a `char` array.
- **`a` and `&a`:** same address, types `int *` and `int (*)[5]`; `a + 1` moves 4 bytes, `&a + 1` moves 20.
- **Valid pointers:** an element, one past the end, or null. Anything else is undefined when formed.
- **Parameters:** `int a[5]` means `int *a`; pass the length. `sizeof` there is 8.
- **Strings:** `char a[] = "abc"` is a writable copy (4 bytes); `char *p = "abc"` points at read-only storage (write: `SIGSEGV`, exit 139). Use `const char *`.
- **2D:** row-major, `m[i][j]` is element `i * cols + j`; make the last index the inner loop (3 times to 13 times faster here).
- **`Node **`:** points at the pointer that points at the node; removal and insertion need no head case.
- **Declarations:** read from the name, right then left; use a `typedef` for function pointers.
- **`qsort` comparators:** `(x > y) - (x < y)`, never `x - y`.
- **`void *` code:** pass the element size, step through `unsigned char *`; no type checking, about 5 to 6 times slower byte-wise here.
- **Dangling and out of bounds:** undefined; "it works" at `-O0` proves nothing; use ASan and UBSan.

---

## Sources

- The program outputs are measured here (GCC 13.3, `-std=c11`, x86-64 Linux, LP64, one run each for the programs; the helper code was tested with 6,219,416 checks under ASan and UBSan, and 20 deliberate bugs were all caught). The timings are measured on one Xeon container, two runs each, with identical results printed. The data is random and seeded. Statements about ARM64, Clang, other C libraries, C++ `std::sort` and `qsort` internals are marked "from memory" or "not measured here".
- The rules about decay, pointer arithmetic, one past the end, parameter adjustment, string literals, `const` and conversions between pointer types are from the C standard (C11 sections 6.3.2.1, 6.5.6, 6.7.6.3, 6.4.5 and 6.3.2.3; the wording and examples here are my own). For the exact text, read the C11 draft N1570.
- The `extern int *a;` against `int a[N];` story and the declaration-reading method follow *Expert C Programming* (van der Linden), as I remember them; check the book for the details.
- Problem numbers and titles in section 15.2 are LeetCode's, from memory; check a number before you rely on it.
