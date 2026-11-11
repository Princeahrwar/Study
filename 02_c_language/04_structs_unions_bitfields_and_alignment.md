# C 04: Structs, Unions, Bit-fields and Alignment

*Part of the [C language plan](00_plan.md) · Time: about 4 hours (2.5 reading, 1.5 problems) · You need: day-to-day C, a compiler (`gcc` or `clang`), [C 02](02_pointers_and_arrays.md) for pointers and [C 03](03_memory_stack_heap_and_malloc.md) for where objects live. There is no companion demo file: every program below was compiled and run, and its real output is printed under it*

A struct is where C meets the hardware. The same declaration decides how much memory an object takes, whether a network packet is parsed correctly, whether a register write does what the datasheet says, and whether your code crashes on a different CPU. The rules are few and mechanical: each member sits at a multiple of its alignment, the struct is padded to a multiple of its strictest member, and almost everything else (bit-field order, byte order, packing, what a union read means) is either implementation-defined or a trap. This tutorial states each rule, shows it in bytes, measures what it costs, and ends with the two idioms that replace the risky features: shifts and masks for registers and wire formats, and `memcpy` for reinterpreting bytes.

By the end you can:

- compute the size and every member offset of a struct by hand, and reorder members to remove padding (a calculator is printed and tested against the compiler);
- explain why padding exists, why `memcmp` on a struct is wrong, and why copying a struct to user space can leak data;
- say what `packed` does, what unaligned access costs on x86-64 (measured) and what the rule is on ARM (from memory), and read unaligned data portably with `memcpy`;
- explain endianness, convert between host and network order, and parse a wire header byte by byte instead of casting the buffer;
- write the Internet checksum and check it against a known header;
- list what the standard leaves implementation-defined about bit-fields, say why drivers and protocol code avoid them, and replace them with `field_get` and `field_set`;
- use a union for type punning (and know that `memcpy` is always safe), and say what changes in C++;
- allocate a message with a flexible array member safely (overflow-checked size) and compare it with a pointer member;
- use `_Alignas`, `aligned_alloc` and `_Static_assert` to fix and check a layout.

**Where the numbers come from.** Every program was compiled with GCC 13.3 (`-std=c11 -Wall -Wextra`) on an x86-64 Linux container (LP64: `int` 4 bytes, `long` and pointers 8; little endian) and run; the outputs are what it printed. The helper code printed in the tutorial (the layout calculator, the byte-order helpers, the field helpers, the checksum, the header parser and the flexible message) is the real source, tested against oracles: 287,621 checks passed under AddressSanitizer and UBSan. The layout calculator is checked against the compiler's own `sizeof`, `offsetof` and `_Alignof` for eight structs; the checksum against the RFC 1071 example and a second implementation; the byte-order functions against `__builtin_bswap32`; the field helpers against a bit-by-bit model. I tried 22 deliberate bugs in them (no tail padding, a swapped byte pair, a one-time carry fold in the checksum, a field write that spills into its neighbour, a size sum that can wrap, and others); 21 were caught, 1 of them by the sanitizer. The one survivor is explained in section 8: it is equivalent on a 64-bit target. Statements about ARM, 32-bit x86, Clang and the exact wording of the standard are from memory or from the standard and are marked.

**Data and timing.** Machine: a cloud container, Intel Xeon 2.10 GHz, GCC 13.3.0, `-O2` for timings, one thread. Times are the best of 3 repetitions, shown as run 1 / run 2. Data is random from a seeded generator, so every run gives the same sums. Timings show the shape of the cost on this machine; the ratios will differ on yours, and on ARM.

---

## 1. Alignment and padding

Every type has an **alignment**: its address must be a multiple of that number. On x86-64 Linux (the System V ABI) the alignment of a scalar equals its size: `char` 1, `short` 2, `int` 4, `long` and pointers 8, `double` 8, `long double` 16. (32-bit x86 Linux differs: `double` and `long long` are 4-aligned inside structs; from memory.) CPUs want this because a load that straddles a boundary may need two memory accesses, or may not be allowed at all (section 3).

A struct is laid out by two rules, applied in declaration order:

1. each member is placed at the **next multiple of its own alignment** at or after the end of the previous member; the gap is **padding**;
2. the struct's alignment is the **largest member alignment**, and its size is rounded **up** to a multiple of that (**tail padding**), so that every element of an array of the struct is aligned.

No rule lets the compiler reorder members: they stay in declaration order, which is what makes `offsetof` and binary file formats possible. The calculator below is those two rules in code.

```c
/* The layout rule: each member goes at the next multiple of its alignment, and the
   whole struct is rounded up to a multiple of its strictest member's alignment. */
typedef struct {
    size_t size, align;                      /* align must be a power of two */
} Member;

size_t align_up(size_t x, size_t a) { return (x + a - 1) & ~(a - 1); }

/* Fills offsets[i] and returns the struct size; *struct_align gets the alignment. */
size_t layout(const Member *m, size_t n, size_t *offsets, size_t *struct_align) {
    size_t off = 0, al = 1;
    for (size_t i = 0; i < n; ++i) {
        off = align_up(off, m[i].align);
        offsets[i] = off;
        off += m[i].size;
        if (m[i].align > al) al = m[i].align;
    }
    *struct_align = al;
    return align_up(off, al);
}

/* Bytes of padding that a given order costs. */
size_t padding(const Member *m, size_t n) {
    size_t offs[16], al, total = layout(m, n, offs, &al), used = 0;
    for (size_t i = 0; i < n; ++i) used += m[i].size;
    return total - used;
}

/* Sort by alignment, largest first: the order with the least padding when sizes are
   multiples of their alignment (true for every scalar type). */
void sort_by_align(Member *m, size_t n) {
    for (size_t i = 1; i < n; ++i) {
        Member key = m[i];
        size_t j = i;
        while (j > 0 && m[j - 1].align < key.align) { m[j] = m[j - 1]; --j; }
        m[j] = key;
    }
}
```

`padding` tells you what an ordering costs, and `sort_by_align` puts the strictest members first: when every size is a multiple of its alignment (true of all scalars and arrays of them), descending alignment gives the minimum padding. Here are five structs, run through the compiler:

```c
#include <stdio.h>
#include <stddef.h>

struct Bad  { char a; int b; char c; short d; double e; char f; };
struct Good { double e; int b; short d; char a; char c; char f; };
struct Tail { int x; char c; };               /* tail padding */
struct Arr  { char tag; int vals[3]; };
struct Wide { char a; _Alignas(32) char b; };   /* C11 over-alignment */

#define SHOW(T) printf("%-12s size %2zu  align %2zu\n", #T, sizeof(struct T), _Alignof(struct T))

int main(void) {
    SHOW(Bad); SHOW(Good); SHOW(Tail); SHOW(Arr); SHOW(Wide);
    printf("Bad : a@%zu b@%zu c@%zu d@%zu e@%zu f@%zu\n", offsetof(struct Bad, a), offsetof(struct Bad, b),
           offsetof(struct Bad, c), offsetof(struct Bad, d), offsetof(struct Bad, e), offsetof(struct Bad, f));
    printf("Good: e@%zu b@%zu d@%zu a@%zu c@%zu f@%zu\n", offsetof(struct Good, e), offsetof(struct Good, b),
           offsetof(struct Good, d), offsetof(struct Good, a), offsetof(struct Good, c), offsetof(struct Good, f));
    printf("array of 3 Tail: %zu bytes, second element's x at offset %zu\n", sizeof(struct Tail[3]), sizeof(struct Tail));
    return 0;
}
```

```text
Bad          size 32  align  8
Good         size 24  align  8
Tail         size  8  align  4
Arr          size 16  align  4
Wide         size 64  align 32
Bad : a@0 b@4 c@8 d@10 e@16 f@24
Good: e@0 b@8 d@12 a@14 c@15 f@16
array of 3 Tail: 24 bytes, second element's x at offset 8
```

`Bad` and `Good` hold the same six members. `Bad` puts a `char` before an `int`, a `char` and a `short` before a `double`, and ends on a `char`: 3 + 1 + 4 + 7 = 15 bytes of padding around 17 bytes of data, 32 in all. `Good` sorts them by alignment and takes 24 (7 bytes of padding, all at the tail), a saving of 25 percent with no change in content (problem A works one through by hand). `Tail` shows rule 2: `int` plus `char` is 5 bytes, but `sizeof` is 8, because an array of `Tail` must keep each `x` aligned; the second element starts at offset 8. `Wide` shows that `_Alignas` (C11) raises a member's alignment and the struct follows: size 64 for two `char`s.

**What the padding contains.** Nothing you can rely on. A member assignment does not write the padding, and a whole-struct copy may or may not copy it. Two consequences:

```c
#include <stdio.h>
#include <string.h>

struct Rec { char tag; int value; };

static void dump(const char *label, const struct Rec *r) {
    const unsigned char *p = (const unsigned char *)r;
    printf("%-8s", label);
    for (size_t i = 0; i < sizeof *r; ++i) printf(" %02x", p[i]);
    printf("\n");
}

int main(void) {
    struct Rec a, b;
    memset(&a, 0xFF, sizeof a);       /* garbage in the padding */
    memset(&b, 0x00, sizeof b);       /* zeros in the padding   */
    a.tag = 'x'; a.value = 7;
    b.tag = 'x'; b.value = 7;
    dump("a:", &a);
    dump("b:", &b);
    printf("fields equal: %d\n", a.tag == b.tag && a.value == b.value);
    printf("memcmp says equal: %d\n", memcmp(&a, &b, sizeof a) == 0);
    return 0;
}
```

```text
a:       78 ff ff ff 07 00 00 00
b:       78 00 00 00 07 00 00 00
fields equal: 1
memcmp says equal: 0
```

The two records have equal fields and different bytes, so `memcmp(&a, &b, sizeof a)` says they differ. **Never compare structs with `memcmp`** (C has no `==` for structs; write a field-wise function), and never hash a struct's raw bytes unless you control the padding (a `packed` struct, or one you zeroed with `memset` first and only ever copied whole). The second consequence is a security one: a struct copied to another trust domain carries its padding bytes, which may hold old stack contents. The Linux kernel has had many information-leak CVEs of exactly this form; the cure is `memset` before filling a struct that is copied to user space (from memory; an initialiser `= {0}` is not guaranteed to clear padding by the standard, though GCC usually does).

**Layout is part of an interface.** A struct shared between a library and its callers, written to a file, or sent between machines is a binary contract. `_Static_assert(sizeof(struct hdr) == 12, "hdr layout")` and `_Static_assert(offsetof(struct hdr, id) == 4, "id offset")` turn an accidental change into a compile error. GCC and Clang can warn on padding (`-Wpadded`), and the `pahole` tool from the dwarves package prints the holes of every struct in a binary (from memory).

**Cost of ordering.** Reordering shrinks the object, and that matters when there are millions of them. The measured effect on speed is another question:

```text
T2 4000000 records: badly ordered struct 32 bytes (122 MB), reordered 24 bytes (91 MB); sums 3995445779 and 3995445779, equal: yes
```

| Sum two fields of each record | Time ms (run 1 / run 2) |
|---|---|
| badly ordered struct (32 bytes, 122 MB) | 9.51 / 8.74 |
| reordered struct (24 bytes, 91 MB) | 10.37 / 9.62 |

The reordered array is 31 MB smaller and was **not faster** in this test: it was about the same, a little slower in both runs. Both versions read the two fields from adjacent cache lines in a streaming loop that the hardware prefetcher handles well, so memory traffic did not limit the loop the way the sizes suggest. Do not promise a speed-up from reordering; promise less memory, fewer cache lines per object, and a better chance when the working set no longer fits in cache. Measure your own case.

## 2. Struct semantics in one page

- **Assignment and by-value passing** copy the whole object, member by member (like `memcpy`, padding aside). A struct can be returned by value too. On x86-64 System V a small struct (up to 16 bytes) travels in registers and a larger one is copied through memory (from memory); for large structs pass a `const struct S *`.
- **No `==`.** Compare field by field.
- **Initialisation:** `struct S s = {1, 2};` in order, `{.id = 7}` by designated name (C99; unnamed members become zero), and a **compound literal** `(struct S){.id = 7}` creates a temporary. Designated initialisers survive reordering of members; positional ones do not.
- **Nesting and arrays:** a struct member of struct type is laid out with that type's alignment; an array member `int vals[3]` keeps the element alignment of 4 (see `Arr` above, size 16).
- **`offsetof(T, m)`** (from `<stddef.h>`) is the byte offset of `m`; subtracting it from a member's address gives the container's address, which is how the kernel's `container_of` works (C 09).
- **Incomplete types:** `struct Node;` can be declared and pointed to before it is defined; that is how a `Node` holds a `Node *` and how a header hides a type (C 09).

## 3. Packing and unaligned access

`__attribute__((packed))` (GCC and Clang; `#pragma pack` in MSVC and also in GCC) removes the padding and drops the alignment of the struct to 1. It is how people describe a wire or file layout without worrying about gaps.

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>

struct Natural { uint8_t kind; uint32_t value; };
struct __attribute__((packed)) Packed { uint8_t kind; uint32_t value; };

int main(void) {
    printf("natural: size %zu, value at offset %zu\n", sizeof(struct Natural), offsetof(struct Natural, value));
    printf("packed : size %zu, value at offset %zu\n", sizeof(struct Packed), offsetof(struct Packed, value));
    unsigned char buf[16];
    for (int i = 0; i < 16; ++i) buf[i] = (unsigned char)i;
    struct Packed *p = (struct Packed *)(buf + 1);        /* a packed struct may start at any address */
    uint32_t v = p->value;                                /* the compiler knows the access may be unaligned */
    uint32_t w;
    memcpy(&w, buf + 2, sizeof w);                        /* the portable way to read an unaligned word */
    printf("value read through the packed struct: %08x\n", (unsigned)v);
    printf("value read with memcpy              : %08x\n", (unsigned)w);
    return 0;
}
```

```text
natural: size 8, value at offset 4
packed : size 5, value at offset 1
value read through the packed struct: 05040302
value read with memcpy              : 05040302
```

The packed struct is 5 bytes against 8, and its `value` member sits at offset 1, so it is almost never aligned. Reading it through the struct pointer works here because the compiler **knows** the member is packed and emits an access that tolerates misalignment. The `memcpy` into a local variable is the portable way to read four bytes from any address; the compiler turns it into a single load on x86 and into whatever is legal on the target on others. (GCC warns when you take the address of a packed member and store it in an ordinary pointer, `-Waddress-of-packed-member`, because that pointer loses the information.)

**What unaligned access costs.**

- **x86-64:** a plain load or store at any address works. The penalty is small unless the access crosses a cache line (64 bytes), where it costs two line accesses, and a page boundary, which costs more. The trace below reads a 64 MiB buffer as 16,777,216 32-bit words, once at offset 0 and once at offset 2.
- **ARM (from memory; check the manual for your core):** ARMv7 and ARMv8-A allow unaligned `LDR` and `STR` to ordinary memory, but not for multi-register loads such as `LDM`, `LDRD`, or for exclusive and atomic instructions; Device-type memory (peripheral registers) requires aligned accesses. Cortex-M0 and M0+ fault on any unaligned access, and Cortex-M3 and M4 allow single loads and stores but fault on the multi-word forms. A packed struct accessed through a compiler that knows it is packed is safe (it emits byte accesses where needed); a cast pointer is not.
- **Elsewhere (from memory):** older MIPS, SPARC and PowerPC generations trap or are slow.

```text
T3 64 MiB buffer: big-endian by shifts 36020016932776775, by memcpy and swap 36020016932776775, equal: yes; native loads aligned 36034514262930335, at offset 2 36029965750964680 (different data, same count of 16777216 loads)
```

| Read 16,777,216 32-bit words | Time ms (run 1 / run 2) |
|---|---|
| aligned, offset 0 | 8.63 / 8.88 |
| offset 2 (crossing a cache line every 16th word) | 14.36 / 14.34 |

The misaligned reads are about 1.6 times slower here, as expected when one word in 16 spans two cache lines. The second place where alignment shows up in measurements is a packed array:

```text
T1 8000000 records: natural struct is 8 bytes (61 MB), packed is 5 bytes (38 MB); sums 3996437105 and 3996437105, equal: yes
```

| Sum the `value` of 8,000,000 records | Time ms (run 1 / run 2) |
|---|---|
| natural struct, 8 bytes each (61 MB) | 6.68 / 7.27 |
| packed struct, 5 bytes each (38 MB) | 5.78 / 6.11 |

On this CPU the packed array is faster, not slower: the loop is limited by memory bandwidth, packing reads 38 percent fewer bytes, and x86 handles the misaligned loads at no visible cost. That result **does not carry over to ARM cores that trap or split unaligned accesses**, or to code that takes pointers to packed members. Packing for size is a legitimate choice; packing "to be fast" is a measurement you make on the target.

**The cast that is not allowed.** Reading a `uint32_t` by casting a `char *` into the middle of a buffer breaks two rules: the address may be misaligned for the type (undefined), and the access violates the effective-type rules (C 01). The program below does it on purpose under UBSan:

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>

int main(int argc, char **argv) {
    (void)argv;
    size_t off = (size_t)argc;                        /* 1: a run-time offset the compiler cannot fold */
    unsigned char buf[8] = {0, 0x78, 0x56, 0x34, 0x12, 0, 0, 0};
    uint32_t viamemcpy;
    memcpy(&viamemcpy, buf + off, 4);                 /* fine: copies four bytes, any address */
    printf("memcpy read at offset 1: 0x%08x\n", (unsigned)viamemcpy);
    uint32_t viacast = *(uint32_t *)(buf + off);      /* undefined: buf + 1 is not aligned for uint32_t */
    printf("cast read at offset 1  : 0x%08x\n", (unsigned)viacast);
    return 0;
}
```

```text
p8_cast.c:12:14: runtime error: load of misaligned address ADDR for type 'uint32_t', which requires 4 byte alignment
memcpy read at offset 1: 0x12345678
cast read at offset 1  : 0x12345678
```

UBSan reports the misaligned load at the cast. On x86-64 the program still prints the right value, which is exactly why this bug survives testing on a laptop and fails on a Cortex-M or in an optimised build that vectorises the loop. `memcpy` does the same job legally, and the compiler removes the call.

## 4. Endianness and network byte order

**Endianness** is the order in which the bytes of a multi-byte value are stored. **Little endian** puts the least significant byte at the lowest address (x86, ARM in its usual mode, RISC-V); **big endian** puts the most significant byte first (many network protocols, older PowerPC, SPARC, s390x). The Internet protocols are defined as big endian: **network byte order**.

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>

int main(void) {
    uint32_t x = 0x01020304;
    unsigned char b[4];
    memcpy(b, &x, 4);
    printf("0x01020304 in memory: %02x %02x %02x %02x  (%s endian)\n", b[0], b[1], b[2], b[3], b[0] == 4 ? "little" : "big");
    uint32_t n = htonl(x);
    memcpy(b, &n, 4);
    printf("htonl(0x01020304)    : %02x %02x %02x %02x  (network order is big endian)\n", b[0], b[1], b[2], b[3]);
    unsigned char wire[4] = {0x01, 0x02, 0x03, 0x04};
    uint32_t wrong;
    memcpy(&wrong, wire, 4);
    printf("wire bytes 01 02 03 04 read as a native uint32: 0x%08x (wrong)\n", (unsigned)wrong);
    printf("same bytes, ntohl                              : 0x%08x\n", (unsigned)ntohl(wrong));
    return 0;
}
```

```text
0x01020304 in memory: 04 03 02 01  (little endian)
htonl(0x01020304)    : 01 02 03 04  (network order is big endian)
wire bytes 01 02 03 04 read as a native uint32: 0x04030201 (wrong)
same bytes, ntohl                              : 0x01020304
```

`htonl` turns a host value into network order (on a little-endian host it swaps; on a big-endian host it does nothing), and `ntohl`, `htons`, `ntohs` are the inverses. The last two lines show the failure: four wire bytes `01 02 03 04` read into a native `uint32_t` give `0x04030201` on this machine. The bug does not appear on a big-endian test machine, which is why code that passes there can fail in the field.

The byte-order-independent way to read and write wire data is to build the value from bytes, which is correct on every CPU and needs no `ntohl`:

```c
/* Byte-order-independent reads and writes: they work the same on every CPU. */
uint16_t load_be16(const unsigned char *p) { return (uint16_t)(p[0] << 8 | p[1]); }
uint32_t load_be32(const unsigned char *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
uint32_t load_le32(const unsigned char *p) {
    return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}
void store_be16(unsigned char *p, uint16_t v) { p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }
void store_be32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}
uint32_t swap32(uint32_t v) {
    return (v >> 24) | (v >> 8 & 0xFF00u) | (v << 8 & 0xFF0000u) | (v << 24);
}
int host_is_little(void) { uint16_t one = 1; unsigned char b; memcpy(&b, &one, 1); return b == 1; }
```

```text
T3 64 MiB buffer: big-endian by shifts 36020016932776775, by memcpy and swap 36020016932776775, equal: yes; native loads aligned 36034514262930335, at offset 2 36029965750964680 (different data, same count of 16777216 loads)
```

Is the shift version slower than a byte swap? I timed both on a 64 MiB buffer (the same trace as above):

| Read 16,777,216 big-endian 32-bit words | Time ms (run 1 / run 2) |
|---|---|
| four byte loads combined with shifts (`load_be32`) | 20.37 / 18.25 |
| `memcpy` into a `uint32_t` then `swap32` | 20.28 / 19.39 |

They are the same speed, because GCC at `-O2` recognises the shift pattern and compiles it to one load and one byte-swap instruction (I did not inspect the assembly; the identical times and the matching sums are the evidence). So write the portable shift version and let the compiler make it fast.

Rules that follow:

1. Define the byte order of every file and wire format in its specification, and convert at the edge.
2. Never `fwrite` a struct or send it over a socket as it is: padding, packing and endianness all leak into the format.
3. Never cast a receive buffer to a struct pointer: alignment, aliasing and endianness are all wrong.
4. `host_is_little` above is the portable run-time test; compile-time tests (`__BYTE_ORDER__` in GCC and Clang) are for header code.

## 5. Parsing a wire header

Putting these together: a 12-byte header with a version, flags, a length, an id and a checksum, parsed field by field, with the checksum written and verified. The checksum is the **Internet checksum** of RFC 1071, used by IPv4, ICMP, UDP and TCP: the one's complement of the one's complement sum of the 16-bit big-endian words. Two properties make it practical: a message that includes its own correct checksum sums to zero, and a single flipped bit always changes the result.

```c
/* The Internet checksum (RFC 1071): the one's complement of the one's complement sum of
   16-bit big-endian words. A packet that includes its own checksum sums to zero. */
uint16_t inet_checksum(const unsigned char *p, size_t n) {
    uint32_t sum = 0;
    for (; n >= 2; p += 2, n -= 2) sum += load_be16(p);
    if (n) sum += (uint32_t)p[0] << 8;       /* an odd last byte is padded with a zero byte */
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}
```

```c
/* A 12-byte wire header, parsed field by field from bytes, never by casting the buffer. */
typedef struct {
    uint8_t version, flags;
    uint16_t length;
    uint32_t id;
    uint16_t checksum, reserved;
} WireHdr;

enum { WIRE_SIZE = 12 };

int wire_parse(const unsigned char *buf, size_t n, WireHdr *h) {
    if (n < WIRE_SIZE) return -1;
    h->version = buf[0];
    h->flags = buf[1];
    h->length = load_be16(buf + 2);
    h->id = load_be32(buf + 4);
    h->checksum = load_be16(buf + 8);
    h->reserved = load_be16(buf + 10);
    return 0;
}

void wire_write(unsigned char *buf, const WireHdr *h) {
    buf[0] = h->version;
    buf[1] = h->flags;
    store_be16(buf + 2, h->length);
    store_be32(buf + 4, h->id);
    store_be16(buf + 8, 0);                  /* the checksum field counts as zero while summing */
    store_be16(buf + 10, h->reserved);
    store_be16(buf + 8, inet_checksum(buf, WIRE_SIZE));
}
```

The parser checks the length first and then reads each field from its offset with the byte helpers; `wire_write` fills the header and then the checksum. The tests round-trip random headers, check that the checksum verifies, and flip every kind of single bit to see that the verification fails. A real IPv4 header is a good vector: the 20 bytes `45 00 00 73 00 00 40 00 40 11 00 00 c0 a8 00 01 c0 a8 00 c7` have the checksum `0xb861` (problem C, checked below by the program), and the header with that value filled in checksums to `0x0000`.

Parsing untrusted input is also where the safety rules start (C 11): check the length before every read, never trust a length field to be inside the buffer, and compute sizes with overflow checks.

## 6. Bit-fields

A **bit-field** is a struct member with a width in bits: `unsigned mode : 3;`. It looks like a convenient way to name the fields of a register or a header.

```c
#include <stdio.h>
#include <string.h>
#include <stdint.h>

struct Flags {
    unsigned enable : 1;
    unsigned mode   : 3;
    unsigned level  : 4;
};

struct Split {
    unsigned a : 5;
    unsigned b : 5;
    unsigned c : 5;
    unsigned d : 5;
};

int main(void) {
    struct Flags f;
    memset(&f, 0, sizeof f);
    f.enable = 1; f.mode = 5; f.level = 9;
    unsigned char raw[sizeof f];
    memcpy(raw, &f, sizeof f);
    printf("struct Flags: size %zu, bytes", sizeof f);
    for (size_t i = 0; i < sizeof f; ++i) printf(" %02x", raw[i]);
    printf("  (enable=1 mode=5 level=9)\n");
    uint32_t mask_version = 1u | 5u << 1 | 9u << 4;
    printf("the same fields built with shifts, enable at bit 0: 0x%02x\n", (unsigned)mask_version);
    printf("struct Split (4 fields of 5 bits): size %zu\n", sizeof(struct Split));
    struct Flags g = {0};
    unsigned nine = 9;
    g.mode = nine;                                        /* 9 does not fit in 3 bits */
    printf("mode = 9 stored in 3 bits reads back as %u\n", (unsigned)g.mode);
    return 0;
}
```

```text
struct Flags: size 4, bytes 9b 00 00 00  (enable=1 mode=5 level=9)
the same fields built with shifts, enable at bit 0: 0x9b
struct Split (4 fields of 5 bits): size 4
mode = 9 stored in 3 bits reads back as 1
```

On this compiler the first member is the **least significant** bits of the storage unit (`enable` is bit 0, `mode` bits 1 to 3, `level` bits 4 to 7), so the bytes are `9b`, the same as building the value with shifts. The last line shows a silent truncation: 9 stored in a 3-bit field reads back as 1 (GCC warns when the constant is visible; I hid it behind a variable).

What the standard leaves **implementation-defined or unspecified** is the reason serious code avoids bit-fields for anything with an external layout (C11 6.7.2.1; from memory):

1. the **order** in which fields are packed inside a storage unit (low bits first on GCC for little endian, high bits first for most big-endian ABIs);
2. whether a field can **straddle** a storage-unit boundary, and the size and alignment of the unit;
3. whether a plain `int` field is **signed** (use `unsigned` or `signed` explicitly);
4. which types are allowed (`_Bool`, `int`, `unsigned int` are required; GCC accepts `char`, `short` and enums as well);
5. **access width**: the compiler may read or write the whole unit, or a byte; for a device register, an access of the wrong width or a read-modify-write on a write-only or read-to-clear register is a hardware bug, and `volatile` does not say how wide the access is;
6. **atomicity and races**: a run of adjacent bit-fields is a single memory location (C11 5.1.2.4), so two threads writing different fields of the same run race with each other.

The Linux kernel's own network headers show the consequence: `struct iphdr` declares `ihl : 4` and `version : 4` twice, once under `#if defined(__LITTLE_ENDIAN_BITFIELD)` and once for big endian, with the two fields in opposite order (from memory). That is a bit-field making a portability problem the programmer then has to solve by hand. The kernel's drivers use masks instead: `GENMASK`, `FIELD_GET` and `FIELD_PREP` (from memory), which are the macros below in other clothes.

```c
/* A register field without bit-fields: the layout is fixed by you, not the compiler. */
#define MASK(width) ((width) >= 32 ? 0xFFFFFFFFu : ((1u << (width)) - 1u))
uint32_t field_get(uint32_t reg, unsigned shift, unsigned width) {
    return reg >> shift & MASK(width);
}
uint32_t field_set(uint32_t reg, unsigned shift, unsigned width, uint32_t value) {
    uint32_t m = MASK(width) << shift;
    return (reg & ~m) | ((value << shift) & m);   /* an oversized value cannot spill into other fields */
}
```

`field_set` clears the old bits and masks the new value, so a too-large value cannot spill into a neighbouring field (a mutant without the mask was caught). `MASK(32)` is special-cased because `1u << 32` is undefined (a mutant that removed this one was caught by UBSan). Because you write the shifts, the layout is the same on every compiler and CPU, the access width is the one you choose (a 32-bit `volatile` load or store, section 4 of the Embedded track), and the code reads like the datasheet: `field_get(reg, 4, 4)` is "bits 7:4".

Bit-fields are not slow, but they may be slower than the mask code. This trace sums a 4-bit field out of 8,000,000 words:

```text
T4 8000000 registers: sum of the 4-bit field at bit 4 by bit-field 59994948, by shift and mask 59994948, equal: yes
```

| 8,000,000 registers | Time ms (run 1 / run 2) |
|---|---|
| bit-field member `.level` | 7.33 / 7.85 |
| `field_get(w, 4, 4)` | 2.67 / 2.83 |

The mask version is about 2.7 times faster here. The reason I could confirm is vectorisation: with vectorisation turned off (`-fno-tree-vectorize`, one run) the mask loop took 5.0 ms against 7.3 ms for the bit-field, about 1.4 times faster, so most of the gap at `-O2` was the compiler using SIMD on the mask loop and not on the bit-field loop. The lesson is not "bit-fields are slow"; it is that you should not expect them to be faster, and that for registers and wire formats correctness decides.

Where bit-fields are fine: compact in-memory flags that never leave the program, with `unsigned` fields and no layout expectation.

## 7. Unions and type punning

A **union** holds one of its members at a time; all members start at offset 0 and the union is as large as its largest member (rounded to its strictest alignment).

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>

union Bits { float f; uint32_t u; };

int main(void) {
    union Bits b = {.f = 1.0f};
    printf("union read of 1.0f   : 0x%08x\n", (unsigned)b.u);
    float f = 1.0f;
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    printf("memcpy read of 1.0f  : 0x%08x\n", (unsigned)u);
    union { uint8_t bytes[4]; uint32_t word; } v = {.word = 0x11223344};
    printf("bytes of 0x11223344  : %02x %02x %02x %02x\n", v.bytes[0], v.bytes[1], v.bytes[2], v.bytes[3]);
    union { char c; int i; double d; } big;
    printf("union {char; int; double}: size %zu, all members at offset 0\n", sizeof big);
    return 0;
}
```

```text
union read of 1.0f   : 0x3f800000
memcpy read of 1.0f  : 0x3f800000
bytes of 0x11223344  : 44 33 22 11
union {char; int; double}: size 8, all members at offset 0
```

Writing one member and reading another is **type punning**. In C, reading a different member than the one last written reinterprets the bytes (C99 TC3 and C11 footnote; GCC documents it as supported). Here `1.0f` has the IEEE 754 bit pattern `0x3f800000`, and four bytes of `0x11223344` come out as `44 33 22 11`, the byte order from section 4. Three cautions:

1. The result can be a **trap representation** or an unspecified value if the sizes differ or the bytes do not form a valid value of the type read (an invalid `_Bool`, a signalling `float` on some targets).
2. **C++ forbids it** (reading an inactive member is undefined there; `std::bit_cast` in C++20 and `memcpy` are the legal ways). Code that must compile as both uses `memcpy`.
3. The other way to punning, casting a pointer (`*(uint32_t *)&f`), is **undefined** because it violates the effective type rules (strict aliasing, C 01). It often "works" at `-O0` and breaks at `-O2`. Only the character types may alias anything.

`memcpy(&u, &f, sizeof u)` is correct in every case, at every optimisation level, and compiles to a register move. Use it by default; use a union when you want the reader to see the two views.

Unions also give you the **tagged union** (a union plus a field saying which member is live), the standard C way to write a variant type; C 09 builds one.

## 8. Flexible array members

C99 lets the **last** member of a struct be an array without a size: `unsigned char data[];`. It is not counted in `sizeof`, and you allocate the struct and the array together.

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

struct Flex { uint32_t len; unsigned char data[]; };
struct Ptr  { uint32_t len; unsigned char *data; };

int main(void) {
    printf("sizeof(struct Flex) = %zu, offsetof(data) = %zu\n", sizeof(struct Flex), offsetof(struct Flex, data));
    printf("sizeof(struct Ptr)  = %zu\n", sizeof(struct Ptr));
    struct Flex *f = malloc(sizeof *f + 10);          /* one allocation: header and ten bytes */
    f->len = 10;
    memset(f->data, 7, f->len);
    printf("Flex: one malloc, data[9] = %d\n", f->data[9]);
    struct Ptr *p = malloc(sizeof *p);                /* two allocations, two frees, two cache lines */
    p->len = 10;
    p->data = malloc(p->len);
    memset(p->data, 7, p->len);
    printf("Ptr : two mallocs, data[9] = %d\n", p->data[9]);
    struct Flex *copy = malloc(sizeof *copy + f->len);
    memcpy(copy, f, sizeof *f + f->len);              /* copying the whole message is one memcpy */
    printf("copy of Flex is one memcpy of %zu bytes\n", sizeof *f + f->len);
    free(copy); free(p->data); free(p); free(f);
    return 0;
}
```

```text
sizeof(struct Flex) = 4, offsetof(data) = 4
sizeof(struct Ptr)  = 16
Flex: one malloc, data[9] = 7
Ptr : two mallocs, data[9] = 7
copy of Flex is one memcpy of 14 bytes
```

`sizeof(struct Flex)` is 4 and `data` starts at offset 4 (`offsetof` gives the true start; `sizeof` can include trailing padding, so use `offsetof` when you need the exact size). The alternative with a pointer member costs two allocations, two frees, 16 bytes of header (the pointer forces 8-byte alignment), and the data lives somewhere else in memory. With the flexible array, the message is **one block**: it can be copied with one `memcpy`, freed with one `free`, and a pointer to it is a pointer to the data. Restrictions: the array must be last; the struct cannot be a member of another struct or an element of an array (GCC allows it at the end of another struct as an extension); assigning a struct copies only the fixed part, not the array; and it needs at least one other member.

Allocation is where the bug lives: `malloc(sizeof(Msg) + len)` can wrap around when `len` is huge, giving a tiny block that the caller then fills with `len` bytes (the C 03 calloc story again). The helper checks before it adds:

```c
/* A message with a flexible array member: header and bytes in one allocation. */
typedef struct {
    uint32_t len;
    unsigned char data[];
} Msg;

Msg *msg_new(size_t len) {
    if (len > UINT32_MAX || len > SIZE_MAX - sizeof(Msg)) return NULL;   /* the size sum must not wrap */
    Msg *m = malloc(sizeof(Msg) + len);
    if (m) m->len = (uint32_t)len;
    return m;
}
```

The first test, `len > UINT32_MAX`, is there because `len` is stored in a 32-bit field (a mutant that removed it was caught). The second test, `len > SIZE_MAX - sizeof(Msg)`, is the wrap check. One of the 22 mutants removed it and **survived**, and it is worth knowing why: on a 64-bit target `len > UINT32_MAX` already rejects every value close to `SIZE_MAX`, so the second test never fires, and the mutant is equivalent here. On a 32-bit target the two conditions differ and the second one is needed; keep it because the code should be right wherever it is compiled, not only where the tests ran. The Linux kernel wraps this pattern in `struct_size()` (from memory), and recent kernels annotate the length member with `__counted_by` so that the compiler can bounds-check the array (also from memory).

Older code writes `data[1]` or the GNU `data[0]`; both are tricks that make the compiler or sanitizers complain about out-of-bounds accesses. Use `[]` in new code.

## 9. Alignment tools

- **`_Alignof(T)`** (`alignof` with `<stdalign.h>`) gives the alignment; **`_Alignas(n)`** (`alignas`) raises the alignment of a variable or member; `__attribute__((aligned(n)))` is the GCC form.
- **`aligned_alloc(alignment, size)`** (C11) and `posix_memalign` allocate memory aligned to a power of two larger than `malloc`'s 16; C11 requires `size` to be a multiple of `alignment` (glibc is lenient, other libraries are not; from memory).
- **Cache lines.** On x86-64 and most ARM cores a cache line is 64 bytes (from memory; some Apple cores use 128). Two variables that different threads write, if they share a line, make that line bounce between cores: **false sharing**. The fix is `_Alignas(64)` on each hot variable or padding the struct to a line. Conversely, put fields that are used together in the same line.
- **DMA and SIMD** often require 16, 32 or 64 byte alignment of buffers; a driver allocates DMA buffers with the allocator that guarantees it.
- **`_Static_assert(cond, "message")`** (C11) checks sizes and offsets at compile time.
- **Variable-length data after a struct** needs the next item aligned: use `align_up` (section 1) to place it.

## 10. What it costs

| Question | Answer (this machine, GCC 13.3, `-O2`) |
|---|---|
| Padding in a struct of 6 mixed members | 15 bytes against 7 in the sorted order; 32 against 24 bytes |
| Speed gain from reordering members | none measured (about the same, 4,000,000 records, memory 31 MB less) |
| Packed array against natural | faster here (38 against 61 MB, bandwidth bound); not a rule on other CPUs |
| Misaligned 32-bit load, crossing a cache line every 16th word | about 1.6 times slower |
| Big-endian read with shifts against `memcpy` plus swap | the same speed (compiler fuses it) |
| Bit-field read against `field_get` mask | mask about 1.4 times faster scalar, 2.7 times with SIMD |
| Union or `memcpy` pun | free; compiles to a register move |
| Flexible array against pointer member | one allocation instead of two, 12 bytes less header here |

## 11. Common mistakes

1. **`memcmp` or `==` on structs.** Padding makes equal values compare different (section 1). Compare fields.
2. **Sending a struct on the wire or writing it to a file.** Padding, packing and endianness leak in; write fields one by one.
3. **Casting a buffer to a struct pointer.** Misaligned, aliasing-violating and in host byte order. Parse with byte helpers or `memcpy`.
4. **Assuming `sizeof` is the sum of the members.** Add padding; assert it with `_Static_assert`.
5. **Taking the address of a packed member and storing it in a plain pointer.** The pointer loses the misalignment information; on ARM this can fault.
6. **Bit-fields for hardware registers or wire formats.** Order, width and endianness depend on the compiler and the CPU.
7. **A signed bit-field.** `int x : 1` holds 0 and -1 (or is implementation-defined); use `unsigned`.
8. **Leaking padding bytes.** A struct copied to another trust domain carries stale stack; `memset` it first.
9. **`malloc(sizeof(Msg) + len)` without a wrap check.** Check `len` first (section 8).
10. **Reading a union member that is not the live one without thinking about trap values or C++.** Use `memcpy` when the code must be portable.
11. **Reordering members in a published struct.** The layout is part of the interface for libraries and files; add at the end, or version the struct.
12. **Using `htonl` on 64-bit values.** `htonl` is 32-bit; `htons` is 16-bit. 64-bit needs `htobe64` (from `<endian.h>`, glibc) or a manual version.

## 12. Questions out loud

1. **What is the size of `struct { char a; int b; char c; }`, and how do you shrink it?** 12 bytes: `a` at 0, `b` at 4, `c` at 8, tail padding to 12. Order `b, a, c` gives 8. Sort by alignment.
2. **Why does padding exist?** Aligned access is faster or required, and arrays of structs need each element aligned. The cost is memory; the control you have is member order, `packed`, and `_Alignas`.
3. **Why can't you compare structs with `memcmp`?** Padding bytes are unspecified and can differ between equal structs. Compare field by field.
4. **What does `packed` do, and what are the risks?** Removes padding and sets alignment to 1; unaligned access (costly or faulting on some CPUs), pointers to members lose the packed information, and the layout is compiler-specific.
5. **Little endian against big endian; what is network byte order?** The byte order of multi-byte values in memory. Network order is big endian. Convert at the edges with `htonl` and friends or by building values from bytes.
6. **How would you parse a network header safely?** Check the buffer length, read each field from its offset with byte helpers (or `memcpy` into a local), convert the byte order, validate lengths and ranges, never cast the buffer to a struct.
7. **Why do drivers avoid bit-fields?** The standard does not fix the bit order, the unit, or the access width; endianness changes the order; a read-modify-write may be illegal on a register. Use shifts and masks with explicit 32-bit volatile accesses.
8. **How do you reinterpret a float's bits?** `memcpy` into a `uint32_t` (always correct) or a union (fine in C, undefined in C++). Not a pointer cast.
9. **What is a flexible array member, and why use it?** An unsized last array; the struct and its data are one allocation, one `free`, one `memcpy`. Use `offsetof(S, data)` and an overflow-checked size.
10. **How do you get a 64-byte-aligned buffer?** `aligned_alloc(64, size)` with a size that is a multiple of 64, `posix_memalign`, or a static array declared `_Alignas(64)`.
11. **What is false sharing?** Different threads writing different variables that share a cache line; the line bounces between cores. Align or pad the hot variables to separate lines.
12. **A struct is 12 bytes on your machine and 16 on the other side of the wire. What happened?** Different ABI (alignment of `long`, `double` or `long long`), different packing, or padding on one side only. Define the format by field offsets and sizes, not by a struct.

---

## Problems

Difficulty is marked E (entry level), M (medium) or H (hard). Do A to C on paper first.

**A (E, on paper).** For `struct { char a; short b; int c; char d; double e; char f[3]; }` on x86-64, give every offset and `sizeof`. Then reorder the members to minimise the size, and give the new offsets and size. How many bytes of padding were removed?

**B (E, on paper).** A 16-bit register has the value `0x1234`. A driver declares `struct { unsigned a : 4; unsigned b : 12; }`, copies the two register bytes into the first two bytes of it (little-endian machine, GCC), and reads the fields. What do `a` and `b` read? Name three things that could make another compiler or CPU give a different answer, and write the portable version with `field_get`.

**C (M, on paper then in your editor).** Compute the Internet checksum of the 20-byte IPv4 header `45 00 00 73 00 00 40 00 40 11 00 00 c0 a8 00 01 c0 a8 00 c7` (the checksum field is zero). Do the 16-bit additions by hand, fold the carry, complement. Then check your answer with the code in section 5, and verify that the header with your checksum filled in sums to zero.

**D (M, LeetCode).** *Reverse Bits* (190) and *Convert a Number to Hexadecimal* (405), both from memory; the second is about two's complement and handling negative numbers with unsigned arithmetic. For each, say what changes between a little-endian and a big-endian machine (answer: nothing, because they work on values, not on bytes in memory), and why that is the point of using shifts.

**E (H, in your editor).** Write `tlv_next`: a function that walks a buffer of **type-length-value** records (1 byte type, 2 byte big-endian length, then that many bytes), returns the next record, and rejects a truncated record, a length that runs past the end of the buffer, and a total that would overflow. Write a test with random buffers and a model that builds records, and one that feeds random garbage under ASan and UBSan. This is the shape of most network and file parsers.

**Where to solve them.** A to C on paper first, then check. D on LeetCode (190 and 405; the numbers and constraints are from memory, so check the statements). E in your editor with `-std=c11 -Wall -Wextra -fsanitize=address,undefined`; the section 5 tests are the template.

**How to approach them.**

- **A:** place each member at the next multiple of its alignment, then round the total up; for the reordered version sort by alignment, largest first, and finish with the three `char`s.
- **B:** the register value is `0x1234`; on this compiler the first field is the low bits. For the big-endian rule, `a` takes the top four bits. A shift and a mask say exactly which bits you mean.
- **C:** add the ten 16-bit words as plain integers, fold carries until the value fits 16 bits, then invert; remember the checksum field counts as zero.
- **D:** use `uint32_t`, test one bit at a time with a shift, and for 405 convert a negative `int` to `unsigned` first.
- **E:** write the failing tests first: a length of 0xFFFF in a 10-byte buffer, a header with only two of its three bytes, and an offset near `SIZE_MAX`. Compare the remaining length, never the sum of offset and length.

## Answers

**A.** (Program output below.) Original order: `a` at 0, `b` at 2 (one byte of padding), `c` at 4, `d` at 8, `e` at 16 (seven bytes of padding), `f` at 24 to 26, tail padding to 32. Size 32, alignment 8, 13 bytes of padding. Reordered: `e` at 0, `c` at 8, `b` at 12, `a` at 14, `d` at 15, `f` at 16 to 18, tail padding to 24. Size 24, 5 bytes of padding, all at the tail. Eight bytes removed in all.

**B.** The register `0x1234` is stored as bytes `34 12` on a little-endian machine, and on this compiler the first bit-field is the low bits, so `a` is the low four bits, 4, and `b` is the next twelve bits, `0x123` (program output below). Another compiler or CPU can differ in: (1) the **order** in which bit-fields are allocated (an ABI that starts at the most significant bit splits the same bits differently); (2) the **byte order** of the copy (the bytes would arrive swapped on a big-endian machine); (3) the **size of the storage unit** (this struct is 4 bytes because the unit is `unsigned`, not the 2 bytes of the register). The portable code is `field_get(reg, 0, 4)` and `field_get(reg, 4, 12)`: it gives the same answer on every machine, because the datasheet says which bits.

**C.** The ten 16-bit words are `4500, 0073, 0000, 4000, 4011, 0000, c0a8, 0001, c0a8, 00c7`. Their sum as plain integers is `0x2479c`. Fold the carry (`0x2` goes back into the low 16 bits): `0x479c + 0x2 = 0x479e`. Complement: `0xffff - 0x479e = 0xb861`. With that value stored in the checksum field, the same sum becomes `0xffff` and the checksum of the whole header is `0x0000`. The program output confirms both:

```text
A original: a@0 b@2 c@4 d@8 e@16 f@24 size 32
A reordered: e@0 c@8 b@12 a@14 d@15 f@16 size 24
A calculator: size 32, padding 13; after sort_by_align padding 5
B bytes 34 12 as a struct: a = 4, b = 0x123
B same value 0x1234 with field_get: low 4 bits = 4, next 12 bits = 0x123
C checksum of the 20 byte header: 0xb861; checksum over the header with it filled in: 0x0000
```

**D.** *Reverse Bits* (190): loop 32 times, `result = (result << 1) | (n & 1); n >>= 1`. *Convert to Hex* (405): cast to `unsigned`, emit four bits at a time from the low end, reverse; 0 is a special case. Neither depends on the machine's byte order because shifts operate on values.

**E.** An outline, not a full solution. `tlv_next(buf, len, *pos, *rec)`: if `*pos == len` return end; if `len - *pos < 3` return error (truncated header); read type from `buf[*pos]` and length with `load_be16(buf + *pos + 1)`; if `length > len - *pos - 3` return error; set the record to point at `buf + *pos + 3` with the length, advance `*pos` by `3 + length`. Every comparison is of the form "remaining against needed", so no sum can wrap. The model builds a random list of records into a buffer and expects to read them back; the garbage test checks that no input crashes and that every successful record lies inside the buffer.

## Cheat sheet

- **Layout:** each member at the next multiple of its alignment; the struct aligned to its strictest member and padded to a multiple of it. Order by descending alignment for the least padding. Members never reorder.
- **Padding:** unspecified bytes. No `memcmp`, no raw hashing, `memset` before copying to another trust domain.
- **Assert layouts:** `_Static_assert(sizeof(T) == n, "")` and `offsetof`.
- **Packed:** no padding, alignment 1; compiler-specific; unaligned access is free-ish on x86 and not guaranteed elsewhere; read with `memcpy`.
- **Endianness:** little (x86, ARM usually, RISC-V) against big (network order). `htonl`, `ntohl`, `htons`, `ntohs`; `htobe64` for 64 bits; or build values from bytes with shifts.
- **Wire parsing:** check length, read fields from offsets with byte helpers, never cast the buffer, never write a struct to the wire.
- **Internet checksum:** one's complement of the one's complement sum of 16-bit big-endian words; a packet with its checksum sums to zero.
- **Bit-fields:** order, unit, width and signedness are implementation-defined; adjacent fields are one memory location. For registers and wire formats use `field_get` and `field_set` with fixed-width volatile accesses.
- **Unions:** all members at offset 0; reading another member is allowed in C, not in C++; `memcpy` is always safe; pointer casts violate aliasing.
- **Flexible array:** `T data[];` last; one allocation; check `len` before adding to `sizeof`; use `offsetof(S, data)`.
- **Alignment tools:** `_Alignof`, `_Alignas`, `aligned_alloc` (size a multiple of alignment), 64-byte lines, false sharing.

## Sources

- [C11 draft N1570](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf): 6.2.8 (alignment), 6.7.2.1 (structure and union specifiers, bit-fields, flexible array members), 6.5 (effective types), 5.1.2.4 (memory locations). I did not re-open the PDF for this tutorial; section numbers are from memory.
- [RFC 1071: Computing the Internet Checksum](https://www.rfc-editor.org/rfc/rfc1071): the algorithm and the example vector used in the tests.
- [GCC manual: Common Type Attributes](https://gcc.gnu.org/onlinedocs/gcc/Common-Type-Attributes.html): `packed` and `aligned`. The page resolves; I did not read the `packed` entry itself, so the `-Waddress-of-packed-member` behaviour I describe is from my own compile and memory.
- [GCC manual: Structures, Unions, Enumerations, and Bit-Fields (implementation-defined behavior)](https://gcc.gnu.org/onlinedocs/gcc/Structures-unions-enumerations-and-bit-fields-implementation.html): how GCC allocates bit-fields and which types it accepts.
- [System V AMD64 ABI](https://gitlab.com/x86-psABIs/x86-64-ABI): the alignment table and the argument-passing rules for structs (I used my memory of it, not the current text).
- [Linux kernel: `include/uapi/linux/ip.h`](https://github.com/torvalds/linux/blob/master/include/uapi/linux/ip.h): `struct iphdr` with the two bit-field orders (cited from memory; the page could not be fetched from here because of a rate limit).
