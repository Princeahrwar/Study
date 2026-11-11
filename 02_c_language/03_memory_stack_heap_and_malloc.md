# C 03: Memory: Stack, Heap and malloc

*Part of the [C language plan](00_plan.md) · Time: about 4 hours (2.5 reading, 1.5 problems) · You need: day-to-day C, a compiler (`gcc` or `clang`), [C 01](01_types_conversions_and_undefined_behavior.md) for undefined behaviour and [C 02](02_pointers_and_arrays.md) for pointers. There is no companion demo file: every program below was compiled and run, and its real output is printed under it*

Every object in a C program lives somewhere, and where it lives decides how long it lasts, who frees it, how fast it is to create, and which bugs it can have. Most "memory" interview questions ("why did this crash", "what does `free` know", "how would you write `malloc`", "when would you not use `malloc`") are one idea asked four ways. This tutorial gives you the map (static data, stack, heap), shows what a stack frame and a heap block look like in bytes, builds a working allocator you can read in one sitting, shows why a heap with plenty of free memory can still refuse a request, and then covers the two things that beat `malloc` in practice (arenas and pools) and the tools that find memory bugs (AddressSanitizer and Valgrind).

By the end you can:

- name the four storage durations (static, automatic, allocated, thread) and say which region of the process each one lives in;
- explain a stack frame, why the stack grows down, what the limit is, how to estimate recursion depth, and what happens at the limit;
- explain what `malloc` and `free` do inside glibc: chunk header, size classes, bins, `brk` against `mmap`, and how `free` knows how many bytes to release;
- read and write a first-fit allocator with splitting, coalescing and boundary tags, and say what its weak points are;
- explain external and internal fragmentation with numbers, and say why a heap can have 512 KiB free and refuse a 2000-byte request;
- write an arena (bump) allocator and a fixed-size pool, and say when each beats `malloc`;
- explain `calloc` against `malloc` plus `memset` (including the multiplication overflow check), `alloca` and variable-length arrays and why they are dangerous;
- recognise a heap overflow, use after free, double free, leak, stack overflow and an uninitialised read, and say which of AddressSanitizer and Valgrind catches each.

**Where the numbers come from.** Every program was compiled with GCC 13.3 (`-std=c11 -Wall -Wextra`) on an x86-64 Linux container (LP64: `int` 4 bytes, pointers 8, glibc 2.39) and run; the outputs are what it printed. The allocator source printed in the tutorial (arena, pool, first-fit heap with split, coalesce, realloc and a consistency checker) is the real source, tested against a model: 8,112,576 checks passed under AddressSanitizer and UBSan. The checks fill every live block with a pattern and verify the patterns, so an overlap or a corrupted block is caught. I tried 28 deliberate bugs in it (no alignment, a missing coalesce, a stale `prev_size`, a double free that goes unnoticed, a realloc that leaks the old block, and others); 26 were caught, 9 of them by the sanitizer rather than by a wrong answer. The two that survive are discussed in section 4: one is an equivalent change, one is a redundant check. Everything about glibc internals (chunk layout, bins, thresholds) describes glibc 2.39 as I measured it and as I remember its documentation; it is an implementation detail, not something C promises, and other allocators (musl, jemalloc, tcmalloc, the Windows heap) differ. Statements about other platforms are from memory and marked.

**Data and timing.** Machine: a cloud container, Intel Xeon 2.10 GHz, GCC 13.3.0, `-O2` for timings, one thread, 2 cores, 8 GB RAM. Times are the best of 3 repetitions, shown as run 1 / run 2. The sandbox sets the environment variables `MALLOC_MMAP_THRESHOLD_` and `MALLOC_TRIM_THRESHOLD_`; I removed them (`env -u`) for every run that involves glibc's allocation thresholds, so the numbers below are glibc's defaults. Timings show the shape of the cost on this machine; the ratios will differ on yours.

---

## 1. Storage durations and the process memory map

The C standard talks about **storage duration**, which is how long an object lives, not where it lives. There are four:

| Duration | How you get it | Lives until | Typical region |
|---|---|---|---|
| **Static** | file scope variables, and `static` locals | the program ends | `.data` (initialised), `.bss` (zero), read-only data (constants and string literals) |
| **Automatic** | ordinary locals and parameters | the block or function returns | the stack |
| **Allocated** | `malloc`, `calloc`, `realloc` | `free` | the heap, or a mapping |
| **Thread** | `_Thread_local` (C11) | the thread ends | thread-local storage, one copy per thread |

Duration is separate from **scope** (where the name is visible) and **linkage** (whether other files can name it; C 05). A `static` local has block scope but static duration: one object, created before `main`, still alive after the function returns. That is why it keeps its value and why it is not thread safe.

On Linux the loader and kernel lay a process out as follows, lowest address first: the **code** (`.text`, read-only and executable), read-only data (constants, string literals), **`.data`** (globals with a non-zero initial value, copied from the file), **`.bss`** (globals that start at zero; the file only records the size), the **heap** (grows up from just above `.bss` through `brk`), a large region of **memory mappings** (shared libraries, big allocations, `mmap`), and the **stack** at the top (grows down). The kernel randomises the base of each region on every run (ASLR), so absolute addresses change from run to run; relative order and region names do not, which is why the programs below print labels, not addresses.

```c
#include <stdio.h>
#include <stdlib.h>

int global_init = 7;          /* .data  */
int global_zero;              /* .bss   */
const char *literal = "hi";   /* the pointer is in .data, the characters in read-only memory */

static int counter(void) {
    static int calls;         /* static storage: one object for the whole run */
    return ++calls;
}

int main(void) {
    int local = 1;                      /* automatic storage: the stack */
    int *heap = malloc(sizeof *heap);   /* allocated storage: the heap  */
    int a = counter(), b = counter(), c = counter();   /* separate statements: argument order is unspecified */
    printf("%d %d %d\n", a, b, c);
    printf("code    < data/bss  : %d\n", (void *)main < (void *)&global_init);
    printf("data    < bss       : %d\n", (void *)&global_init < (void *)&global_zero);
    printf("bss     < heap      : %d\n", (void *)&global_zero < (void *)heap);
    printf("heap    < stack     : %d\n", (void *)heap < (void *)&local);
    printf("literal in code area: %d\n", (void *)literal < (void *)&global_init);
    free(heap);
    return 0;
}
```

```text
1 2 3
code    < data/bss  : 1
data    < bss       : 1
bss     < heap      : 1
heap    < stack     : 1
literal in code area: 1
```

The first line shows the static counter keeping its value across calls. The order lines show the layout from the previous paragraph: code below data, data below bss, bss below the heap, heap below the stack. A word on the first line: if you write `printf("%d %d %d\n", counter(), counter(), counter())` the three calls may run in any order, because the order in which function arguments are evaluated is unspecified; the first version of this program printed `3 2 1`. Separate statements fix the order.

`.data` against `.bss` matters for embedded work: `.bss` costs no space in the binary or in flash, only RAM at start-up (the start-up code zeroes it), while a large initialised array makes the binary bigger and has to be copied from flash into RAM. `const` data that stays constant can sit in flash and use no RAM at all (Embedded track).

To see where an address really is, read `/proc/self/maps`. This program asks which mapping contains four addresses and prints the mapping's label:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Which mapping contains the address? Prints the label, never the address. */
static void where(const char *what, void *p) {
    FILE *f = fopen("/proc/self/maps", "r");
    char line[512];
    unsigned long target = (unsigned long)p;
    while (fgets(line, sizeof line, f)) {
        unsigned long lo, hi;
        char name[256] = "";
        if (sscanf(line, "%lx-%lx %*s %*s %*s %*s %255s", &lo, &hi, name) >= 2 && target >= lo && target < hi) {
            printf("%-12s -> %s\n", what, name[0] ? name : "(anonymous mapping)");
            fclose(f);
            return;
        }
    }
    fclose(f);
    printf("%-12s -> not found\n", what);
}

int main(void) {
    void *small = malloc(100);
    void *medium = malloc(100 * 1024);
    void *large = malloc(200 * 1024);
    where("100 bytes", small);
    where("100 KB", medium);
    where("200 KB", large);
    int local;
    where("local int", &local);
    free(small); free(medium); free(large);
    return 0;
}
```

```text
100 bytes    -> [heap]
100 KB       -> [heap]
200 KB       -> (anonymous mapping)
local int    -> [stack]
```

Two things in that output are worth stating. The 100 byte and 100 KB blocks are inside `[heap]`, the area grown with `brk`. The 200 KB block is in an anonymous mapping of its own, made with `mmap`: glibc sends requests above a threshold (128 KiB by default, adjustable with `mallopt` or the environment variable the sandbox sets; the threshold also adapts upward as large blocks are freed; both from the glibc documentation as I remember it) straight to `mmap`, so that `free` can return them to the kernel immediately with `munmap`. Small blocks stay in the `brk` heap, where freed memory is kept for reuse and only trimmed off the top when a large enough gap builds up.

## 2. The stack

Each call to a function creates a **stack frame**: the return address pushed by `call`, the saved frame pointer (when one is used), saved registers, the local variables that do not live in registers, and space for outgoing arguments that do not fit in registers. On x86-64 System V the first six integer arguments go in registers, and the stack pointer is 16-byte aligned at the call. Returning pops the frame by moving the stack pointer back; nothing is cleared, which is why returning a pointer to a local (C 02) "sometimes works".

The stack **grows down** on x86-64, ARM and most others (towards lower addresses); a few architectures grow it up, and the C standard says nothing about it. You can measure it: this program recurses three levels, each with a 64 byte local array, and prints how far apart the arrays are.

```c
#include <stdio.h>
#include <stdint.h>
#include <sys/resource.h>

static uintptr_t depth_addr[4];

static void level(int n) {
    volatile char pad[64];            /* force a real frame */
    pad[0] = (char)n;
    depth_addr[n] = (uintptr_t)&pad[0];
    if (n < 3) level(n + 1);
}

int main(void) {
    level(0);
    for (int i = 1; i < 4; ++i)
        printf("frame %d is %ld bytes below frame %d\n", i, (long)(depth_addr[i - 1] - depth_addr[i]), i - 1);
    struct rlimit rl;
    getrlimit(RLIMIT_STACK, &rl);
    printf("stack limit: %lu KB\n", (unsigned long)(rl.rlim_cur / 1024));
    return 0;
}
```

```text
frame 1 is 112 bytes below frame 0
frame 2 is 112 bytes below frame 1
frame 3 is 112 bytes below frame 2
stack limit: 8192 KB
```

Each frame is 112 bytes: the 64 byte array, plus the saved frame pointer, the return address, the parameter `n`, and padding to keep 16-byte alignment. Each deeper call is at a lower address. This was compiled at `-O0`; an optimiser can shrink a frame, inline calls away, or turn recursion into a loop, so frame size is something you measure for your build, never assume.

**The limit.** The main thread's stack has a fixed maximum (`ulimit -s`; 8192 KB here; the kernel grows it on demand up to that and places an unmapped **guard** area below it). Threads created with `pthread_create` get their own stack, 8 MB of address space by default on glibc (usually taken from the same limit, from memory), and you can set it with `pthread_attr_setstacksize`. Inside the kernel the stack is tiny (a few KB per thread, 16 KB on x86-64; from memory), which is why kernel code never uses big locals or recursion (Kernel track).

An estimate of recursion depth is the limit divided by the frame size. At 112 bytes per frame, 8 MB allows about 74,898 frames (problem A). The real number is a little lower, because the program's environment, arguments and the start-up code are already on the stack. What happens beyond it is a crash, not an error you can catch:

```c
#include <stdio.h>

static long depth;
static long recurse(long n) {
    volatile char pad[256];
    pad[0] = (char)n;
    depth = n;
    return recurse(n + 1) + pad[0];
}

int main(void) {
    printf("start\n");
    fflush(stdout);
    return (int)recurse(0);
}
```

```text
start
exit status 139
```

The program printed `start` and then died with status 139: 128 plus signal 11, `SIGSEGV`. The stack ran into the guard area, the access faulted, and the kernel killed the process. The compiler even warns about this function ("infinite recursion detected"). Real causes are rarely this obvious: a recursive tree walk on a degenerate tree, a big local array (`char buf[1 << 20]` in a function called from a thread with a small stack), or a recursive descent parser fed deeply nested input. A recursive algorithm whose depth depends on the input needs an explicit stack (DSA 06) or a depth limit.

## 3. The heap: what `malloc` and `free` do

The **heap** is memory whose lifetime you control. The allocator (`malloc`, `calloc`, `realloc`, `free`; here glibc's) asks the kernel for big areas (`brk` to grow the main heap, `mmap` for separate mappings) and hands out pieces. Its job is the hard part of memory management: keep a record of which pieces are in use, find a piece of the right size quickly, give pieces back, and not waste memory.

The standard fixes only the contract: `malloc(n)` returns a pointer to at least `n` bytes suitably aligned for any type (16 bytes on x86-64 glibc), or `NULL`; the memory is **uninitialised**; `free(p)` releases it; `free(NULL)` does nothing; freeing anything else, or the same pointer twice, is undefined. `malloc(0)` may return `NULL` or a unique pointer that must not be dereferenced (glibc returns a real minimum-size block). Everything else is implementation.

### 3.1 How `free` knows the size

You pass `free` only a pointer. It finds the size because the allocator stored it **in front of the block**, in a header. In glibc each block is a **chunk**: 8 bytes of `prev_size` (used only when the previous chunk is free, otherwise that space belongs to the previous chunk's data), 8 bytes of `size` with three flag bits in the low bits, and then the memory returned to you. The pointer you get points just past the `size` field. So `((size_t *)p)[-1]` is the size field, and you can read it (never write it):

```c
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    size_t sizes[] = {1, 8, 24, 25, 40, 100, 1000};
    printf("%8s %8s %10s\n", "request", "usable", "size field");
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; ++i) {
        void *p = malloc(sizes[i]);
        size_t field = ((size_t *)p)[-1];
        printf("%8zu %8zu %6zu (flags %zu)\n", sizes[i], malloc_usable_size(p), field & ~(size_t)7, field & 7);
        free(p);
    }
    return 0;
}
```

```text
 request   usable size field
       1       24     32 (flags 1)
       8       24     32 (flags 1)
      24       24     32 (flags 1)
      25       40     48 (flags 1)
      40       40     48 (flags 1)
     100      104    112 (flags 1)
    1000     1000   1008 (flags 1)
```

Reading the table: the chunk size is the request plus 8 bytes of header, rounded up to a multiple of 16, with a minimum of 32. A request for 1 to 24 bytes uses a 32 byte chunk; 25 uses 48; 100 uses 112. `malloc_usable_size` says how much of it you may use (24, 40, 104), which is the chunk size minus 8, because the `prev_size` field of the next chunk is available to you while this chunk is in use. The flag 1 is `PREV_INUSE` (the chunk before this one is allocated), the two other flags are "this chunk was `mmap`ed" and "this chunk belongs to a non-main arena" (from memory). The difference between the request and the chunk is **internal fragmentation**: 24 bytes requested for a 1-byte object is the price of a header and alignment. This is the reason allocating millions of tiny objects with `malloc` is wasteful (section 6).

### 3.2 Free lists and bins

When you call `free`, the chunk is not returned to the kernel. It goes on a list so that the next `malloc` of a similar size can reuse it. glibc keeps several kinds of list (from the glibc malloc wiki as I remember it; the thresholds are for a 64-bit build):

1. **tcache**: a per-thread cache of recently freed chunks, 64 size classes, up to 7 chunks each, for sizes up to about 1 KB. A hit needs no lock and almost no work; this is why `malloc` followed by `free` of the same size is about as cheap as a function call (section 9).
2. **Fastbins and small bins**: singly or doubly linked lists of free chunks of one exact size. Allocation takes the head.
3. **Unsorted bin**: freed chunks land here first, and are sorted into small or large bins on the next allocation that does not find an exact fit.
4. **Large bins**: lists of chunks in a size range, kept sorted, searched for the best fit.
5. **Top chunk**: the free space at the end of the heap. When nothing else fits, `malloc` splits a piece off the top; when the top is too small it calls `brk` for more.

Neighbouring free chunks are merged (**coalesced**) so a freed run of small blocks can serve a big request, except tcache and fastbin chunks, which are left unmerged on purpose to stay fast. The details change between glibc versions; the structure (header with size and flags, exact-size caches in front, sorted bins behind, a top chunk, `brk` and `mmap` underneath) is common to dlmalloc, ptmalloc and most others.

### 3.3 What `free` checks

glibc does a few cheap sanity checks and aborts when one fails. The program below triggers three of them; the argument chooses the bug.

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    int which = argc > 1 ? atoi(argv[1]) : 0;
    char *p = malloc(which == 3 ? 2000 : 24);   /* 2000 bytes is too big for the tcache, so free() takes the slow path */
    char *q = malloc(which == 3 ? 2000 : 24);   /* keeps p from being next to the top of the heap */
    (void)q;
    fflush(stdout);
    if (which == 1) { free(p); free(p); }                 /* double free */
    if (which == 2) { free(p + 1); }                      /* pointer that malloc never returned */
    if (which == 3) { memset(p, 'A', 2040); free(p); }      /* overflow into the next chunk's header */
    printf("survived %d\n", which);
    return 0;
}
```

```text
$ ./p5_abort 1      (free(p); free(p);)
free(): double free detected in tcache 2
exit status 134

$ ./p5_abort 2      (free(p + 1);)
free(): invalid pointer
exit status 134

$ ./p5_abort 3      (2040 bytes written into a 2000 byte block, then free(p);)
free(): invalid next size (normal)
exit status 134
```

Exit status 134 is 128 plus signal 6 (`SIGABRT`): the allocator called `abort`. The first message works because a freed tcache chunk has a marker written into it, and `free` finds the marker already there. The third works because the overflow overwrote the size field of the chunk that follows, and `free` looks at the next chunk's size when it merges. These checks catch only some bugs, and only sometimes: a double free of a chunk that has meanwhile been reallocated, or an overflow into the user data of the next chunk, passes silently and corrupts the heap. The abort often happens far from the bug, in a later `free` or `malloc`. This is why the sanitizers in section 8 exist.

## 4. Write your own allocator

The best way to learn what `malloc` does is to write the smallest one that is correct. The design here is the textbook **implicit free list with boundary tags**, first fit, over one buffer you hand it:

- every block starts with a 16-byte header: the size of the **previous** block, and its own size with the lowest bit meaning "in use";
- block sizes are multiples of 16, and the smallest block is 32 (a header plus 16 bytes), so payloads are 16-byte aligned;
- `heap_malloc` walks the blocks from the start, takes the first free one that is big enough, and **splits** it when at least 32 bytes would be left over;
- `heap_free` clears the in-use bit and **coalesces** with the next block (found by adding the size) and the previous block (found by subtracting `prev_size`), so two free blocks are never side by side;
- `heap_realloc` shrinks in place, grows in place when the next block is free and large enough, and otherwise allocates, copies and frees;
- `heap_free` validates the pointer (inside the buffer, aligned, header says in use, next block's `prev_size` agrees) and returns -1 for a foreign pointer or a double free, where glibc would abort or corrupt;
- `heap_check` walks the whole buffer and verifies every invariant; the tests call it after every operation.

The helper functions come first, then the allocator.

```c
/* A first-fit allocator over one buffer. Every block starts with a 16-byte header:
   the size of the previous block (for merging backwards) and its own size, whose
   lowest bit says whether it is in use. Block sizes are multiples of 16. */
typedef struct {
    size_t prev_size;
    size_t size_used;
} Hdr;

typedef struct {
    unsigned char *base;
    size_t size;
} Heap;

typedef struct {
    size_t used_blocks, free_blocks, used_bytes, free_bytes, largest_free;
} HeapStats;

#define HDR 16
#define MIN_BLOCK 32

static size_t blk_size(const Hdr *b) { return b->size_used & ~(size_t)1; }
static int blk_used(const Hdr *b) { return (int)(b->size_used & 1); }
static Hdr *blk_next(const Heap *h, Hdr *b) {
    unsigned char *n = (unsigned char *)b + blk_size(b);
    return n < h->base + h->size ? (Hdr *)n : NULL;
}
static size_t block_for(size_t n) {          /* total block size for a payload of n bytes */
    size_t s = ((n + 15) & ~(size_t)15) + HDR;
    return s < MIN_BLOCK ? MIN_BLOCK : s;
}

/* buf must be 16-byte aligned and at least 32 bytes. */
void heap_init(Heap *h, void *buf, size_t size) {
    h->base = buf;
    h->size = size & ~(size_t)15;
    Hdr *b = (Hdr *)h->base;
    b->prev_size = 0;
    b->size_used = h->size;                  /* one free block, lowest bit clear */
}

/* Turn the tail of b into a new free block when at least MIN_BLOCK bytes remain. */
static void split(Heap *h, Hdr *b, size_t need) {
    size_t rest = blk_size(b) - need;
    if (rest < MIN_BLOCK) return;
    b->size_used = need | (b->size_used & 1);
    Hdr *r = (Hdr *)((unsigned char *)b + need);
    r->prev_size = need;
    r->size_used = rest;
    Hdr *after = blk_next(h, r);
    if (after) after->prev_size = rest;
}

void *heap_malloc(Heap *h, size_t n) {
    if (n == 0) n = 1;
    if (n > h->size) return NULL;
    size_t need = block_for(n);
    for (Hdr *b = (Hdr *)h->base; b; b = blk_next(h, b)) {
        if (!blk_used(b) && blk_size(b) >= need) {
            split(h, b, need);
            b->size_used |= 1;
            return (unsigned char *)b + HDR;
        }
    }
    return NULL;
}

/* Merge b with a free neighbour on each side; returns the merged block. */
static Hdr *coalesce(Heap *h, Hdr *b) {
    Hdr *next = blk_next(h, b);
    if (next && !blk_used(next)) {
        b->size_used += blk_size(next);
        Hdr *after = blk_next(h, b);
        if (after) after->prev_size = blk_size(b);
    }
    if (b->prev_size != 0) {
        Hdr *prev = (Hdr *)((unsigned char *)b - b->prev_size);
        if (!blk_used(prev)) {
            prev->size_used += blk_size(b);
            Hdr *after = blk_next(h, prev);
            if (after) after->prev_size = blk_size(prev);
            b = prev;
        }
    }
    return b;
}

/* Returns the block of a pointer that heap_malloc returned, or NULL if p cannot be one. */
static Hdr *block_of(Heap *h, void *p) {
    unsigned char *u = p;
    if (u < h->base + HDR || u >= h->base + h->size) return NULL;
    if ((size_t)(u - h->base) % 16 != 0) return NULL;
    Hdr *b = (Hdr *)(u - HDR);
    if (!blk_used(b) || blk_size(b) < MIN_BLOCK) return NULL;
    if ((unsigned char *)b + blk_size(b) > h->base + h->size) return NULL;
    Hdr *next = blk_next(h, b);
    if (next && next->prev_size != blk_size(b)) return NULL;   /* header does not look right */
    return b;
}

/* 0 on success (also for NULL), -1 for a pointer that is not an allocated block. */
int heap_free(Heap *h, void *p) {
    if (!p) return 0;
    Hdr *b = block_of(h, p);
    if (!b) return -1;
    b->size_used &= ~(size_t)1;
    coalesce(h, b);
    return 0;
}

void *heap_realloc(Heap *h, void *p, size_t n) {
    if (!p) return heap_malloc(h, n);
    if (n == 0) { heap_free(h, p); return NULL; }
    Hdr *b = block_of(h, p);
    if (!b) return NULL;
    size_t need = block_for(n);
    if (need > blk_size(b)) {                /* try to take the free block after it */
        Hdr *next = blk_next(h, b);
        if (next && !blk_used(next) && blk_size(b) + blk_size(next) >= need) {
            b->size_used += blk_size(next);
            Hdr *after = blk_next(h, b);
            if (after) after->prev_size = blk_size(b);
        }
    }
    if (need <= blk_size(b)) {
        size_t before = blk_size(b);
        split(h, b, need);
        if (blk_size(b) < before) coalesce(h, blk_next(h, b));   /* the new tail may touch a free block */
        return p;
    }
    size_t old = blk_size(b) - HDR;
    void *q = heap_malloc(h, n);
    if (!q) return NULL;                     /* the old block is left alone */
    memcpy(q, p, old < n ? old : n);
    heap_free(h, p);
    return q;
}

/* Walks every block and checks the invariants; returns the number of blocks, or -1. */
long heap_check(const Heap *h) {
    long count = 0;
    size_t prev = 0;
    int prev_free = 0;
    unsigned char *p = h->base;
    while (p < h->base + h->size) {
        Hdr *b = (Hdr *)p;
        size_t s = blk_size(b);
        if (s < MIN_BLOCK || s % 16 != 0 || p + s > h->base + h->size) return -1;
        if (b->prev_size != prev) return -1;
        if (!blk_used(b) && prev_free) return -1;   /* two free blocks side by side */
        prev = s;
        prev_free = !blk_used(b);
        p += s;
        ++count;
    }
    return p == h->base + h->size ? count : -1;
}

HeapStats heap_stats(Heap *h) {
    HeapStats st = {0, 0, 0, 0, 0};
    for (Hdr *b = (Hdr *)h->base; b; b = blk_next(h, b)) {
        if (blk_used(b)) { ++st.used_blocks; st.used_bytes += blk_size(b); }
        else {
            ++st.free_blocks; st.free_bytes += blk_size(b);
            if (blk_size(b) - HDR > st.largest_free) st.largest_free = blk_size(b) - HDR;
        }
    }
    return st;
}
```

Three details deserve a second look.

**Alignment and size rounding.** `block_for` turns a request into a block size: round the request up to a multiple of 16, add the 16 byte header, never less than 32. A request of 1 byte uses 32; 17 uses 48; 33 uses 64 (problem B). The `prev_size` field is why backward merging is O(1): without it you would have to walk from the start to find the previous block.

**Splitting.** Without a split, a 4000 byte block handed to a 40 byte request wastes 3960 bytes inside the block. With it, the remainder becomes a free block of its own, provided it is at least `MIN_BLOCK`; a smaller remainder stays inside the block as internal fragmentation, because a block of less than 32 bytes could not hold a header and a payload.

**Validation.** `block_of` checks five things before `heap_free` trusts a pointer. A real allocator needs few of them in normal operation, but when you are debugging, returning an error is worth more than a guess. Two of the 28 mutants survived my tests, and they teach something. Removing the `% 16` alignment check changes nothing the tests can see, because a misaligned pointer is rejected by the later checks (the header it would read there is not a plausible one); the check is **redundant defence**, not dead code, but I could not construct an input where only it saves you. Replacing `blk_size(b) > largest` by `blk_size(b) - HDR > largest` in the statistics changes no result, because sizes are multiples of 16, so the two comparisons can only disagree where the stored value would be the same; that is an **equivalent mutant**. A mutant that no test can tell apart is either redundant or equivalent, and it is worth knowing which.

The trace below runs the same workload on this heap and on glibc: 200000 random allocate and free operations on 256 slots, sizes 16 to 512, with a checksum over the data to show the two allocators returned working memory:

```text
T1 200000 random allocate/free operations, 256 slots, sizes 16 to 512: checksum own heap 6339968, glibc 6339968, equal: yes; own heap back to one free block: yes
```

Times, in milliseconds for the whole workload:

| Allocator | Time (run 1 / run 2) |
|---|---|
| Our first-fit heap | 25.91 / 30.24 |
| glibc `malloc` and `free` | 2.83 / 3.73 |

The simple heap is about nine times slower here, with at most 256 live blocks. The reasons are the structure, not the code quality: first fit walks the list from the start, so a malloc has to step over every used block before a free one (the cost grows with the number of blocks), and there is no per-size cache to hit. glibc's tcache answers most of this workload without walking anything. Real allocators keep **segregated free lists** (one list per size class) so that finding a block is O(1) for common sizes, and keep the free blocks separate from the used ones. First fit is the right design for understanding and for small, rarely used heaps (an embedded system with a 16 KB pool, say), and the wrong one for a general-purpose `malloc`.

## 5. Fragmentation

**Internal fragmentation** is memory inside a block that you cannot use: header, alignment padding and rounding. **External fragmentation** is free memory that is in pieces too small to satisfy a request. The second one is the failure that surprises people, because the free total looks fine. This trace fills a 1 MiB heap with 1024 byte blocks, frees every other block, and asks for 2000 bytes:

```text
T3 filled a 1 MiB heap with 1024 blocks of 1024 bytes; free bytes 0
T3 freed every other block: free bytes 524288 in 512 pieces, largest piece 1008 bytes; a 2000-byte request FAILED
T3 freed the rest: 1 free piece, largest 1048560 bytes; the 2000-byte request now succeeds
```

Half the heap (524,288 bytes) is free, in 512 separate holes of 1008 bytes each, and a request for 2000 bytes fails. Nothing is wrong with the allocator: every free block is surrounded by used blocks, and a block must be contiguous. Free the other half and coalescing joins everything back into one piece, after which the same request succeeds. Fragmentation grows when objects of different sizes and different lifetimes are interleaved: a long-lived small object allocated in the middle of a pile of short-lived ones pins the whole region.

What helps, in order of how much it helps:

1. **Do not allocate.** Use the stack, a static array, or one big allocation sliced up yourself. Embedded and real-time code often forbids `malloc` after initialisation for this reason, and for the fact that its time is not bounded.
2. **Group by lifetime.** Objects that die together should be allocated together, from an arena (section 6) that is released in one step.
3. **Group by size.** A pool per object type cannot fragment: every block is the same size, so any freed block fits any later request (section 6). Size-segregated lists in real allocators exist for the same reason.
4. **Coalesce and prefer good fits.** Boundary tags and first fit or best fit keep holes joinable. (Wilson's survey, in the sources, found that address-ordered first fit and best fit both give low fragmentation on real programs; from memory.)
5. **Choose a better allocator.** jemalloc and tcmalloc are designed around size classes and thread caches and often cut fragmentation and contention in a long-running server (from memory; measure your own workload).

glibc also has `malloc_trim` and returns the top of the heap automatically when it has a large enough free tail, but it cannot move a live block to close a hole: C pointers are plain addresses, so a compacting allocator is impossible without handles or indirection.

## 6. Arena and pool allocators

For many programs the right answer to "how do I allocate" is "not with `malloc`". Two allocators written in a few lines beat it on speed, fragmentation, and bookkeeping.

### 6.1 Arena (bump) allocator

An **arena**, also called a region or linear allocator, is a buffer and an offset. Allocation rounds the offset up to the alignment, returns that address and moves the offset on. There is no `free` of single objects: you release everything at once by setting the offset back to a mark (or to zero). That sounds restrictive and fits a large class of code: everything allocated while handling one request, parsing one file, building one frame or running one compiler pass has the same lifetime.

```c
/* A bump allocator: allocate by moving an offset, free everything at once. */
typedef struct {
    unsigned char *base;
    size_t size, used;
} Arena;

void arena_init(Arena *a, void *buf, size_t size) {
    a->base = buf;
    a->size = size;
    a->used = 0;
}

/* align must be a power of two. Returns NULL when the arena is full. */
void *arena_alloc(Arena *a, size_t n, size_t align) {
    uintptr_t cur = (uintptr_t)a->base + a->used;
    uintptr_t aligned = (cur + align - 1) & ~(uintptr_t)(align - 1);
    size_t pad = (size_t)(aligned - cur);
    size_t avail = a->size - a->used;
    if (pad > avail || n > avail - pad)      /* written so that it cannot overflow */
        return NULL;
    a->used += pad + n;
    return (void *)aligned;
}

size_t arena_mark(const Arena *a) { return a->used; }
void arena_reset_to(Arena *a, size_t mark) { a->used = mark; }
```

The overflow check is written as `n > avail - pad` after `pad > avail`, so that no addition can wrap around. The obvious version, `used + pad + n > size`, can overflow for a huge `n` and wrongly succeed; the tests include an allocation of `(size_t)-1` for exactly this reason (and a mutant that checks `pad + n` against `avail + 1` was caught).

Properties: allocation is an add and a compare, with no header, no per-object memory overhead except alignment padding, no fragmentation, excellent cache locality because consecutive allocations are adjacent. Cost: objects cannot be freed individually, so a long-lived arena that keeps growing leaks until reset. Also: destructors do not run, and pointers into the arena die at the reset, so the discipline is "everything in this arena has the same lifetime", not "allocate and forget". Arenas are everywhere: compilers (an AST arena freed after the pass), game engines (a frame arena reset every frame), web servers (a request arena), and the Linux kernel's `memblock` at boot (from memory).

### 6.2 Pool (fixed-size block) allocator

A **pool** (or slab) allocator manages blocks of one size. The free blocks form a linked list **inside the free blocks themselves**: a free block's first 8 bytes hold the pointer to the next free one, so the list costs no extra memory. Allocation pops the head; free pushes the freed block on the head; both are a few instructions and O(1).

```c
/* Fixed-size blocks in a free list. Allocate and free are O(1) and cannot fragment. */
typedef struct PoolNode {
    struct PoolNode *next;
} PoolNode;

typedef struct {
    unsigned char *base;
    size_t block, count, in_use;
    PoolNode *free_list;
} Pool;

/* buf must be 16-byte aligned. Returns the number of blocks. */
size_t pool_init(Pool *p, void *buf, size_t buf_size, size_t block) {
    block = (block + 15) & ~(size_t)15;      /* room for the next pointer, 16-byte aligned */
    if (block < sizeof(PoolNode)) block = sizeof(PoolNode);
    p->base = buf;
    p->block = block;
    p->count = buf_size / block;
    p->in_use = 0;
    p->free_list = NULL;
    for (size_t i = p->count; i > 0; --i) {  /* first block ends up at the head */
        PoolNode *node = (PoolNode *)(p->base + (i - 1) * block);
        node->next = p->free_list;
        p->free_list = node;
    }
    return p->count;
}

void *pool_alloc(Pool *p) {
    PoolNode *node = p->free_list;
    if (!node) return NULL;
    p->free_list = node->next;
    ++p->in_use;
    return node;
}

/* Returns 0, or -1 when ptr is not a block of this pool (or nothing is allocated). */
int pool_free(Pool *p, void *ptr) {
    unsigned char *b = ptr;
    if (b < p->base || b >= p->base + p->count * p->block) return -1;
    if ((size_t)(b - p->base) % p->block != 0 || p->in_use == 0) return -1;
    PoolNode *node = (PoolNode *)b;
    node->next = p->free_list;
    p->free_list = node;
    --p->in_use;
    return 0;
}
```

Properties: O(1) both ways, no external fragmentation (any freed block fits any later request), no header per block, and memory use is bounded and known in advance, which is why real-time and embedded systems love it. Cost: one size per pool, and the capacity is fixed at creation. The same idea is the **slab allocator** that the Linux kernel uses for `task_struct`, inodes and network buffers (`kmem_cache_alloc`; Kernel track), and the free-list inside `tcache`.

The danger of the intrusive free list is also its trick: a freed block's first bytes are the `next` pointer, so a use after free reads or writes the allocator's own data. A write there redirects the free list to attacker-chosen memory, which is the basis of a classic heap exploit (C 11).

### 6.3 Measured

One million objects of 32 bytes each, three ways:

```text
T2 arena: 1000000 of 1000000 allocations of 32 bytes succeeded, arena used 32000000 bytes
T2 pool: 1000000 blocks, 1000000 allocated, one more gives NULL
T2 pool after freeing all: in use 0
```

| What is timed (1,000,000 objects of 32 bytes) | Time ms (run 1 / run 2) |
|---|---|
| `malloc` then `free` right away (tcache hit every time) | 7.54 / 11.15 |
| `malloc` all, then `free` all | 18.77 / 17.88 |
| pool: allocate all, then free all | 4.46 / 4.09 |
| arena: allocate all (reset is one store) | 3.12 / 2.70 |

Three conclusions. First, a `malloc` and `free` pair of the same size right after each other is cheap (under 10 ns here) because the tcache returns the chunk just freed; benchmarks that do this are measuring the best case of `malloc`, not a realistic one. Second, when a million objects are live at once, glibc has to find a chunk for every one, and `malloc` then `free` of all of them costs about 18 ns per object in total here, several times the pool or the arena. Third, the arena is the fastest and uses no per-object memory; the pool is a little slower and supports freeing one object. Memory also differs: a 32 byte request needs a 48 byte chunk in glibc (section 3.1), against exactly 32 bytes in the arena and (rounded to 16) in the pool, half as much again for a million objects.

## 7. `calloc`, `alloca` and variable-length arrays

### 7.1 `calloc` against `malloc` plus `memset`

`calloc(n, size)` returns zeroed memory for `n` objects of `size` bytes. It differs from `malloc(n * size)` followed by `memset` in two ways.

**It checks the multiplication.** `n * size` is computed with unsigned arithmetic, which wraps silently; a wrapped product gives a block that is too small, and writes through it overflow the heap. `calloc` detects the overflow and returns `NULL`:

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int *a = calloc(1000000, sizeof *a);
    printf("calloc(1e6, 4) = %s, first element %d\n", a ? "non-null" : "NULL", a[0]);
    free(a);
    size_t huge = (size_t)1 << 63;
    void *b = calloc(huge, 4);                         /* the product overflows size_t: calloc must refuse */
    printf("calloc(2^63, 4) = %s\n", b ? "non-null" : "NULL");
    void *c = malloc(huge * 4);                        /* what a hand-written multiply does: 2^65 wraps to 0 */
    printf("malloc(2^63 * 4) asked for %zu bytes -> %s\n", huge * 4, c ? "non-null" : "NULL");
    free(b); free(c);
    return 0;
}
```

```text
calloc(1e6, 4) = non-null, first element 0
calloc(2^63, 4) = NULL
malloc(2^63 * 4) asked for 0 bytes -> non-null
```

`calloc(2^63, 4)` is refused (the product, 2^65, does not fit in 64 bits). The hand-written multiply wraps to 0 and asks for zero bytes; glibc then returns a valid minimum-size block that the caller believes can hold 2^63 integers. That is the classic integer-overflow-to-heap-overflow bug (it shaped CVEs in image decoders and network stacks for years); the fix is `calloc`, or an explicit check `if (n && size > SIZE_MAX / n)` before `malloc`.

**It can avoid the zeroing.** When the allocator gets a fresh mapping from the kernel, the kernel has already zeroed it, so `calloc` can skip the `memset`. I measured this on 128 MiB:

```text
T4 128 MiB: sampled sum after calloc 0, after malloc+memset 0
```

| 128 MiB | Time ms (run 1 / run 2) |
|---|---|
| `calloc` and `free`, never touched | 0.00 / 0.00 |
| `calloc`, read one byte per page, `free` | 20.11 / 18.35 |
| `calloc`, write one byte per page, `free` | 52.19 / 51.23 |
| `malloc`, `memset` all 128 MiB, `free` | 65.72 / 57.98 |

A `calloc` of 128 MiB that you never touch costs about nothing, because no memory exists yet: the kernel hands out address space, and each 4 KB page is created on first access. Reading a page maps a shared zero page (cheap), writing a page makes the kernel allocate and zero a real one (a page fault, about 52 ms for the 32,768 pages), and the explicit `memset` writes every byte as well as faulting every page. Two cautions: this saving applies to large allocations served by `mmap`; for a small `calloc` from recycled heap memory glibc has to clear the bytes (from memory), so do not expect the same win there. And a first version of my timing loop printed 0.00 ms for `malloc` plus `memset` because the optimiser had removed the work (I did not look at which transformation it applied); the figure above calls `memset` through a function pointer so the compiler cannot. Benchmarks of memory functions need the same care.

### 7.2 `alloca` and variable-length arrays

`alloca(n)` (not standard C; a GCC and BSD extension) and a **variable-length array** (VLA, `int a[n]`; standard in C99, optional since C11, not in C++) allocate on the stack, in the current frame. They are fast (the stack pointer moves) and free themselves when the function returns, and they have four problems: there is **no failure result** (a huge `n` moves the stack pointer past the guard area, and you crash or, worse, step over the guard page and corrupt memory without an immediate fault); the size is usually controlled by the caller or the input, so an attacker or a bug picks how deep you go; the memory cannot outlive the function; and it cannot be freed early, so `alloca` in a loop grows the stack every iteration. For small, bounded sizes they are fine. The Linux kernel removed its VLAs for these reasons and compiles with `-Wvla` (from memory). The safer pattern when the size is small in the common case is a fixed buffer with a heap fallback:

```text
char small[256];
char *buf = n <= sizeof small ? small : malloc(n);
...
if (buf != small) free(buf);
```

## 8. The memory-bug tour: AddressSanitizer and Valgrind

Six bugs cause most memory failures. The program below contains each as a case chosen by the argument:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    int which = argc > 1 ? atoi(argv[1]) : 0;
    if (which == 1) { char *p = malloc(8); p[8] = 'x'; free(p); }                      /* heap overflow */
    if (which == 2) { int *p = malloc(sizeof *p); *p = 1; free(p); printf("%d\n", *p); } /* use after free */
    if (which == 3) { int *p = malloc(sizeof *p); free(p); free(p); }                  /* double free */
    if (which == 4) { char *p = malloc(100); p[0] = 1; p = NULL; (void)p; }            /* leak */
    if (which == 5) { char buf[8]; memset(buf, 0, 9); printf("%d\n", buf[0]); }        /* stack overflow */
    if (which == 6) { int *p = malloc(4 * sizeof *p); if (p[0] == 0) puts("zero"); free(p); } /* uninitialised read */
    printf("done %d\n", which);
    return 0;
}
```

**AddressSanitizer** (ASan, `-fsanitize=address`) is built into GCC and Clang. The compiler instruments every load and store with a check against **shadow memory** (one shadow byte describes 8 application bytes), the runtime replaces `malloc` and `free` with versions that put poisoned **redzones** around every block and keep freed blocks in a **quarantine** instead of reusing them at once, and the first invalid access prints a report and stops the program. The cost is roughly 2x slower and 2x to 3x more memory (from the ASan documentation, from memory). Compile with `-g -fno-omit-frame-pointer` to get readable stack traces. Addresses and process ids are removed from the lines below:

```text
$ ./p6_asan 1        (write one byte past a malloc(8) block)
==ERROR: AddressSanitizer: heap-buffer-overflow on address ADDR at
ADDR is located 0 bytes after 8-byte region [ADDR,ADDR)
SUMMARY: AddressSanitizer: heap-buffer-overflow p6_bugs.c:7 in main

$ ./p6_asan 2        (read a block after free)
==ERROR: AddressSanitizer: heap-use-after-free on address ADDR at
ADDR is located 0 bytes inside of 4-byte region [ADDR,ADDR)
SUMMARY: AddressSanitizer: heap-use-after-free p6_bugs.c:8 in main

$ ./p6_asan 4        (the only pointer to a block is overwritten)
==ERROR: LeakSanitizer: detected memory leaks
Direct leak of 100 byte(s) in 1 object(s) allocated from:
SUMMARY: AddressSanitizer: 100 byte(s) leaked in 1 allocation(s).
```

ASan also reports the double free (`attempting double-free`) and the stack overflow of case 5 (`stack-buffer-overflow`), each with a full stack trace and the line numbers; I checked both while writing this. The leak report (LeakSanitizer, part of ASan on Linux) appears when the program exits.

**Valgrind** (`valgrind --leak-check=full ./prog`) runs the unmodified binary on a virtual CPU and checks every memory access, with no recompilation. It is much slower (10x to 50x is the usual figure, from memory), and it finds one thing ASan does not: reading uninitialised memory (case 6). MemorySanitizer (`-fsanitize=memory`, Clang only) is the compile-time counterpart. Lines are shortened to the first frame:

```text
$ valgrind ./p6_plain 1        (heap overflow)
Invalid write of size 1
   at main (p6_bugs.c:7)
 Address ADDR is 0 bytes after a block of size 8 alloc'd
   at malloc

$ valgrind ./p6_plain 2        (use after free)
Invalid read of size 4
   at main (p6_bugs.c:8)
 Address ADDR is 0 bytes inside a block of size 4 free'd
   at free

$ valgrind --leak-check=full ./p6_plain 4        (leak)
100 bytes in 1 blocks are definitely lost in loss record 1 of 1
   at malloc
   by main (p6_bugs.c:10)

$ valgrind ./p6_plain 6        (uninitialised read)
Conditional jump or move depends on uninitialised value(s)
   at main (p6_bugs.c:12)
```

Which tool finds what:

| Bug | `free` checks in glibc | ASan | Valgrind | Notes |
|---|---|---|---|---|
| Heap buffer overflow | only if it hits a header | yes, at the access | yes, at the access | redzones in ASan; Valgrind tracks block bounds |
| Use after free | no | yes (quarantine) | yes | a reused block hides it from both after enough allocations |
| Double free | sometimes (tcache) | yes | yes | |
| Leak | no | yes, at exit (LeakSanitizer) | yes, at exit | "still reachable" is not a leak report |
| Stack buffer overflow | no | yes | **no** (it does not know stack array bounds) | `-fstack-protector` catches some at return |
| Uninitialised read | no | **no** | yes | MSan (Clang) also finds it |
| Data race | no | no | Helgrind or DRD | ThreadSanitizer is the compile-time tool (DBG 06) |

Use ASan in your tests and CI (fast enough to run the whole suite), Valgrind when you cannot recompile or for uninitialised reads, and run both on any C code before you trust it. Full debugging workflows, gdb and core dumps are DBG 02 to DBG 05.

## 9. What it costs

| Operation | Cost (this machine, glibc 2.39, `-O2`) |
|---|---|
| Automatic variable | a stack pointer subtract per frame, no per-object cost |
| Static variable | free at run time; `.bss` costs RAM only at start-up |
| `malloc` plus `free` of the same small size, back to back | under 10 ns (tcache hit) |
| `malloc` then `free` when many blocks are live | about 18 ns per object (1,000,000 objects) |
| Pool allocate and free | about 4 to 5 ns per object in the same test |
| Arena allocate | about 3 ns per object; free is one store for the whole arena |
| Chunk overhead in glibc | 8 bytes header, rounded to 16, minimum chunk 32 |
| Our first-fit heap, 256 live blocks | about 9 times slower than glibc on a random workload |
| `calloc` of a large block, untouched | about zero; the kernel supplies zero pages on first use |
| First touch of a 4 KB page | a page fault, about 1.6 microseconds (52 ms for 32,768 pages, measured above) |
| Stack depth | limit divided by frame size: about 75,000 frames of 112 bytes at 8 MB |

## 10. Common mistakes

1. **`malloc` without checking for `NULL`.** On Linux with overcommit a large request usually succeeds and the process is killed later when pages are touched (the OOM killer; from memory); on embedded targets it really returns `NULL`. Check, and decide what failure means.
2. **`sizeof` of a pointer.** `malloc(sizeof p)` allocates 8 bytes; use `malloc(n * sizeof *p)`, which stays right if the type of `p` changes.
3. **`n * size` computed by hand.** The multiply can wrap (section 7). Use `calloc`, or check against `SIZE_MAX / size`.
4. **`p = realloc(p, n)`.** When `realloc` fails it returns `NULL` and leaves the old block allocated; the assignment overwrites `p` and the block leaks. Use a temporary: `tmp = realloc(p, n); if (tmp) p = tmp;`.
5. **Freeing twice, or freeing a pointer you did not get from `malloc`.** Pointers into the middle of a block, stack addresses, string literals and pointers already freed. Set `p = NULL` after `free`; `free(NULL)` is a no-op, which turns a double free into nothing.
6. **Use after free, and keeping a pointer into a block across a `realloc`.** The block may have moved. Hold an index or an offset, not a raw pointer, across growth (the vector in C 02 does this).
7. **Writing one byte past the end.** `char *s = malloc(strlen(t)); strcpy(s, t);` forgets the terminator. Allocate `strlen(t) + 1`.
8. **Returning the address of a local, or storing it in a longer-lived structure.** Covered in C 02; the stack frame is gone after the return.
9. **Large locals.** A 1 MB array in a function is a stack overflow waiting for a smaller stack (a thread, an embedded target, a kernel). Make it `static` or allocate it.
10. **Assuming `malloc` memory is zero.** Fresh pages usually are, recycled blocks are not, and the bug appears only in production. Use `calloc` when you need zeros.
11. **Leaking on the error path.** `p = malloc(); q = malloc(); if (!q) return -1;` leaks `p`. The standard pattern is a single exit with `goto cleanup` (C 10).
12. **Mixing allocators.** Memory from `malloc` must go to `free`, from a pool to its pool, from `new` to `delete`; a library that allocates and expects you to free must say which function to use.

## 11. Questions out loud

1. **What are the storage durations, and where does each live?** Static (`.data`, `.bss`, read-only data), automatic (stack), allocated (heap or an `mmap` mapping), thread (thread-local storage). Scope and linkage are separate ideas.
2. **What is in a stack frame, and what happens when the stack is exhausted?** Return address, saved registers, locals, spill slots, outgoing arguments, aligned to 16. At the limit the access faults on the guard area and the process gets `SIGSEGV`; there is no exception. Depth is about limit divided by frame size.
3. **How does `free` know how many bytes to release?** The allocator stores the size in a header just before the block (in glibc 8 bytes before the pointer, with flag bits in the low bits). Many allocators do this; some put the size class in the page that holds the block instead. This is also why overflowing a block corrupts the allocator's metadata, not only your data.
4. **What is the difference between `brk` and `mmap`?** `brk` moves the end of one contiguous heap; freed memory in the middle cannot be returned. `mmap` makes a separate mapping that `munmap` returns at once. glibc uses `brk` for small blocks and `mmap` above a threshold (128 KiB default, adaptive).
5. **What is fragmentation?** Internal: wasted bytes inside blocks (header, alignment). External: free memory in pieces too small for a request. Show the 1 MiB example: half free, a 2000 byte request fails.
6. **How would you implement `malloc`?** Header with size and in-use bit, first or best fit over a free list, split on allocation, coalesce neighbours on free with boundary tags, a way to get more memory (`brk` or `mmap`), alignment to 16. Then say what you would add for speed: size-segregated lists, per-thread caches, a top chunk.
7. **When would you not use `malloc`?** Fixed-lifetime groups of objects (arena), many objects of one size (pool), real-time or safety code (no heap after start-up), tiny sizes where a header doubles the memory, and anywhere the bound on time or memory must be known.
8. **`calloc` against `malloc` plus `memset`?** `calloc` checks the multiplication and can skip the zeroing for fresh pages from the kernel. `malloc(n * size)` can wrap and give a small block.
9. **What is wrong with `alloca` and VLAs?** No failure result, size comes from input, cannot outlive the function, cannot be freed early.
10. **How do you find a memory bug?** ASan in tests, Valgrind when you cannot recompile or for uninitialised reads, `-Wall -Wextra`, static analysis (C 11), and for heap corruption a custom allocator with canaries and a `heap_check` like the one in section 4.
11. **A program works in debug and crashes in release. Why?** Uninitialised memory that was zero in debug, undefined behaviour the optimiser exploited (C 01), a dangling pointer to a stack slot the optimiser reused, or a stack buffer overflow that changed layout.
12. **Why can `free(p); p = malloc(n);` return the same address?** The freed chunk goes to the cache and is the best fit for the next request of that size. Code that depends on "a fresh address" or on old contents is wrong, and use-after-free bugs sometimes appear to work for exactly this reason.

---

## Problems

Difficulty is marked E (entry level), M (medium) or H (hard). Do A to C on paper first.

**A (E, on paper).** From section 2, a recursive function has a 112 byte frame and the stack limit is 8192 KB. Estimate the maximum recursion depth. Then say what changes if the function is compiled with `-O2` and is a simple tail call.

**B (E, on paper).** With the first-fit heap in section 4, what is the block size for requests of 1, 17, 32, 33 and 100 bytes? How much of each block is wasted (block size minus header minus request)? Which glibc chunk sizes do the same requests get (use the table in section 3.1)?

**C (M, on paper).** Fill a 1 MiB heap with 1024 byte blocks (payload 1008, header 16). Free blocks 0 and 1 of every group of 4 (the blocks with index `i % 4 < 2`). How many free pieces are there, how big is the largest, and which of a 2000 byte and a 2033 byte request succeed? Explain using what coalescing does with two adjacent free blocks.

**D (M, in your editor).** LeetCode 2502, *Design Memory Allocator* (from memory: a class with `allocate(size, mID)` that finds the leftmost run of `size` free units and labels it, and `freeMemory(mID)` that frees every block with that label; n up to 1000). Solve it twice: with a plain array and a linear scan, then think about what first fit does to the free space after many `freeMemory` calls. Write down what fragmentation looks like for this API.

**E (H, in your editor).** Extend the allocator of section 4 into a **debugging allocator**: add a 4-byte canary after every payload and one before it, check both in `heap_free` and in a `heap_check_canaries()` walk, fill freed memory with `0xDD` and new memory with `0xCD`, and detect a double free. Write a test that overflows one byte past a block and expects the check to say which block was damaged. This is what Electric Fence, `MALLOC_CHECK_` and the redzones in ASan do at larger scale.

**Where to solve them.** A, B and C on paper first, then check against the answers. D on LeetCode (search "Design Memory Allocator", 2502; the problem number and constraints are from memory, so check the statement). E in your editor: compile with `-std=c11 -Wall -Wextra -fsanitize=address,undefined`, reuse the test harness idea (a random workload with pattern fills and a model).

**How to approach them.**

- **A:** divide the limit by the frame size, and remember the program's start-up already used some of the stack.
- **B:** use `block_for`: round up to 16, add 16, minimum 32; waste is block size minus 16 minus the request.
- **C:** count how many blocks are free and which are neighbours; two adjacent free blocks merge into one block of 2048 whose payload is 2048 minus the header.
- **D:** an array of `n` cells, `allocate` scans for the first run of free cells of the right length; do not optimise before you have a correct version.
- **E:** put the canaries inside the payload area (grow the request by 8 and offset the returned pointer by 4); validate in `block_of`; write the failing test first.

## Answers

**A.** 8192 KB is 8,388,608 bytes; divided by 112 that is about 74,898 frames (program output below), a little lower in practice because the start-up code, `argv`, `environ` and the auxiliary vector are already on the stack. With `-O2` a function whose last action is a call to itself is usually compiled into a loop, using a constant amount of stack, so the depth is limited by nothing but time. This is an optimisation, not a guarantee: C does not promise tail-call elimination, so do not rely on it for correctness.

**B.** Block sizes (from the program output below): 1 byte gives 32, 17 gives 48, 32 gives 48, 33 gives 64, 100 gives 128. Wasted bytes (block minus 16 minus request): 15, 15, 0, 15, 12. glibc chunk sizes from section 3.1: 1 gives 32, 17 gives 32 (up to 24 fits in a 32 byte chunk), 32 gives 48, 33 gives 48, 100 gives 112. glibc wastes less because its header is 8 bytes of overhead per chunk, not 16, and the next chunk's `prev_size` field is used as payload.

**C.** Freeing blocks 0 and 1 of every group of four leaves two adjacent used blocks (2 and 3) between each pair of freed ones. Adjacent freed blocks coalesce, so the 512 freed blocks become 256 free pieces of 2048 bytes each (payload 2032, because the merged block has one header). The 2000 byte request succeeds (2000 is at most 2032), the 2033 byte request fails, although 524,288 bytes are free in total.

```text
B own heap: request 1 -> block 32 bytes
B own heap: request 17 -> block 48 bytes
B own heap: request 32 -> block 48 bytes
B own heap: request 33 -> block 64 bytes
B own heap: request 100 -> block 128 bytes
A predicted depth: 74898 frames
C blocks 1024; free bytes 524288 in 256 pieces, largest 2032
C a 2000-byte request: succeeds
C a 2033-byte request: fails
```

**D.** A correct version is an array of length `n` with 0 for free and the label otherwise: `allocate` scans for the first run of `size` zeros, `freeMemory` clears every cell with the label and counts them. Complexity O(n) per operation, fine for n of 1000. The fragmentation lesson: first fit always takes the leftmost hole, so small allocations accumulate at the front, and after many frees of different sizes the free cells are scattered in holes that a larger `allocate` cannot use, even when the total free is enough. This is the same shape as section 5. The usual real-world remedies are the same too: size classes, or periodically rebuilding (compaction), which is possible here because labels are handles, not raw addresses.

**E.** An outline, not a full solution. Request `n` bytes of the underlying heap as `n + 8`, write the pattern `0xA5A5A5A5` at offsets 0 and `4 + n`, return the pointer at offset 4. In `heap_free`, check both canaries and report which block (by its pointer) is damaged before releasing it; set `p[0..n)` to `0xDD` after the check. `heap_check_canaries()` walks used blocks (the header tells you the size, and you keep `n` in a spare field or round to the block size and look at a fixed place). For the double free, `block_of` already returns `NULL` for a free block, so return -1 and let the wrapper print "double free". The test: allocate 10 bytes, write 11 bytes, free, and expect the report to name the block. The weakness of canaries compared with ASan: they only fire when you call the check (at `free` or in a walk), not at the bad access; a write that skips over the canary is not found.

## Cheat sheet

- **Durations:** static (`.data`, `.bss`, read-only), automatic (stack), allocated (heap or `mmap`), thread (`_Thread_local`).
- **Layout (low to high):** code, read-only data, `.data`, `.bss`, heap (grows up), mappings, stack (grows down). ASLR moves the bases.
- **Stack:** frame = return address, saved registers, locals, spills; 16-byte aligned; limit `ulimit -s` (8 MB); overflow is `SIGSEGV` (exit 139); depth about limit / frame size.
- **`malloc(n)`:** at least `n` bytes, 16-byte aligned, uninitialised, `NULL` on failure. `free(NULL)` is a no-op; double free and foreign pointers are undefined.
- **glibc chunk:** size = (request + 8) rounded to 16, minimum 32; size field is `((size_t *)p)[-1]`; low bits are flags; small blocks from `brk`, above about 128 KiB from `mmap`.
- **Bins:** tcache (per thread, fast), fastbins and small bins (exact size), unsorted, large bins (sorted), top chunk.
- **First fit with boundary tags:** header {prev size, size + in-use bit}; split when the remainder can hold a block; coalesce with both neighbours on free.
- **Fragmentation:** internal (inside blocks) and external (free total fine, no piece big enough).
- **Arena:** bump pointer, free all at once; pool: fixed-size blocks, intrusive free list, O(1), cannot fragment.
- **`calloc`:** zeroed memory and overflow-checked multiply; free for fresh `mmap` pages. `alloca` and VLAs: no failure result, avoid with input-controlled sizes.
- **Tools:** ASan (`-fsanitize=address -g`) for overflows, use after free, double free, leaks, stack overflows; Valgrind for uninitialised reads and no-recompile runs; TSan or Helgrind for races.
- **Never:** `p = realloc(p, n)`, `malloc(sizeof p)`, a hand-made `n * size`, `free` of a non-`malloc` pointer, a pointer into a block across `realloc`.

## Sources

- [glibc wiki: MallocInternals](https://sourceware.org/glibc/wiki/MallocInternals): chunk layout, bins, tcache, arenas (I could not open this page from the sandbox, which blocks automated requests; the link is from memory, and I checked its content against my own measurements of glibc 2.39 instead).
- [malloc(3), mallopt(3), brk(2), mmap(2) manual pages](https://man7.org/linux/man-pages/man3/malloc.3.html): the contract of `malloc`, `calloc`, `realloc`, `free`, and the `mallopt` thresholds.
- [Doug Lea: A Memory Allocator](https://gee.cs.oswego.edu/dl/html/malloc.html): the design that ptmalloc and glibc descend from (boundary tags, bins, top chunk).
- [Wilson, Johnstone, Neely and Boles: Dynamic Storage Allocation, a Survey and Critical Review (1995)](https://www.cs.utexas.edu/~oops/papers.html): the standard survey of fragmentation and allocation policies; I could not open this page from the sandbox (it disallows robots), so the link and the fit comparison I quote are from memory.
- [AddressSanitizer wiki](https://github.com/google/sanitizers/wiki/AddressSanitizer) and [AddressSanitizerAlgorithm](https://github.com/google/sanitizers/wiki/AddressSanitizerAlgorithm): shadow memory, redzones, quarantine, and the cost.
- [Valgrind Memcheck manual](https://valgrind.org/docs/manual/mc-manual.html): what Memcheck detects, including uninitialised reads and leak categories.
- Bryant and O'Hallaron, *Computer Systems: A Programmer's Perspective*, the chapter on dynamic memory allocation: implicit free list, boundary tags, coalescing, first fit (the allocator in section 4 follows that design).
