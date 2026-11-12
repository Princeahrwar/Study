# C 11: Robust and Secure C

*Part of the [C language plan](00_plan.md) · Time: about 6 hours (4 reading, 2 problems) · You need: [C 03](03_memory_stack_heap_and_malloc.md) for the heap and the sanitizers, [C 07](07_strings_stdlib_and_bit_manipulation.md) for strings and integer behaviour, [C 08](08_advanced_pointers_and_pointer_bugs.md) for pointer bugs, [C 10](10_advanced_c_features_and_error_handling.md) for error handling*

Most C security bugs are the same few mistakes: a length that came from outside and was believed, an arithmetic result that wrapped, a string function that did not know the size of its destination, a check that was made on a name and the use that was made on a different file. This tutorial treats them as an engineering problem with four lines of defence: write code that checks every length (sections 1 and 2), test it hard (section 3), let a machine hunt for the inputs you did not think of (section 4), and let the compiler and the platform limit the damage when you missed one (sections 5 to 9). Everything is built around one small binary message parser that starts with the bugs of a first draft and ends with 13.8 million passing checks and a fuzzer that found nothing in 18 million runs.

By the end you can:

- write checked size arithmetic (`size_add`, `size_mul`, `reallocarray`) and string copies that report truncation, and explain why `strncpy` is not one;
- parse untrusted binary input with a rule you can state ("`n - pos` is the room left, and every length is compared with it") and check the result against an independent oracle;
- write unit tests in plain C, measure what they cover with `gcov`, and measure what they would miss with mutants;
- write a libFuzzer harness and an AFL++ build, find a real bug with each, run a differential fuzzer against a reference parser, and say which bugs fuzzing will not find;
- run `-fanalyzer`, `clang-tidy` and `cppcheck`, and say from a measured matrix what each one catches, what it misses and what it falsely reports;
- explain stack smashing, canaries, `_FORTIFY_SOURCE`, NX, ASLR, PIE, RELRO and CET, read them off a binary, and say what each costs;
- explain format-string bugs, integer overflow in size calculations and TOCTOU races, show the exploit shape in a few lines and write the repair;
- name the CERT C rules that matter most and say in outline what MISRA C asks of embedded and automotive code.

**Where the numbers come from.** Every program and command was run on an x86-64 Linux container (Ubuntu, glibc 2.39, GCC 13.3, clang 18.1.3, 2 vCPUs). Code is compiled with `-std=gnu11 -Wall -Wextra -Wno-unused-function` unless the text says otherwise. The parser and the safe helpers are in one header, `sec.h`, printed in sections 1 and 2. The test program has 13,805,215 checks at its default setting and ran clean under `-fsanitize=address,undefined`; it was also run against 24 deliberate defects of the library (section 3). The fuzzers, `cppcheck`, `afl-fuzz` and the libFuzzer runtime were installed in the container for this tutorial (clang's libFuzzer from `libclang-rt-18-dev`, `cppcheck` 2.22.0, AFL++ 4.09c); a fresh Ubuntu does not have them. Two things in this tutorial are from memory and not measured: the CERT C and MISRA C material in section 9 (the CERT pages load, but the fetch tool could not show me the rule titles, and the MISRA documents are paid), and a few details I flag in the text as "from memory".

**Data and timing.** Machine: a cloud container, Intel Xeon 2.10 GHz, 2 vCPUs, GCC 13.3.0, `-O2`. Timings are the best of 3 wall-clock runs in milliseconds, shown as run 1 / run 2 (two complete repetitions of the whole benchmark). Fuzzer throughput is the figure the fuzzer prints itself and varies by 10 to 20 percent from run to run, so take it as an order of magnitude. Output of every demo is printed exactly as it appeared, except that addresses in sanitizer reports are replaced by `ADDR` and process IDs are removed.

---

## 1. The first rule: every length is hostile

A length, a count or an offset that comes from a file, a socket or an argument is attacker data. The bug pattern is always the same: the code uses the number to size a copy or an allocation, and nobody asked whether the number is possible. Three small helpers make the right thing the short thing.

```c
/* Checked size arithmetic: -1 on overflow, and *out is then not meaningful. */
static int size_add(size_t a, size_t b, size_t *out) { return __builtin_add_overflow(a, b, out) ? -1 : 0; }
static int size_mul(size_t a, size_t b, size_t *out) { return __builtin_mul_overflow(a, b, out) ? -1 : 0; }

/* malloc(n * size) with the multiplication checked: NULL and ENOMEM on overflow. */
static void *array_alloc(size_t n, size_t size)
{
    size_t total;
    if (size_mul(n, size, &total)) {
        errno = ENOMEM;
        return NULL;
    }
    return malloc(total ? total : 1);
}

/* strlcpy semantics: copy at most size-1 bytes, always terminate when size > 0,
   return strlen(src) so the caller can detect truncation (result >= size). */
static size_t str_copy(char *dst, size_t size, const char *src)
{
    size_t n = strlen(src);
    if (size > 0) {
        size_t k = n < size - 1 ? n : size - 1;
        memcpy(dst, src, k);
        dst[k] = '\0';
    }
    return n;
}

/* strlcat semantics: append, never write past dst[size-1], return the length it tried to make. */
static size_t str_cat(char *dst, size_t size, const char *src)
{
    size_t dl = strnlen(dst, size);
    if (dl == size)
        return size + strlen(src);       /* dst is not terminated inside the buffer: do nothing */
    return dl + str_copy(dst + dl, size - dl, src);
}
```

- `size_add` and `size_mul` use the compiler's overflow builtins (`__builtin_add_overflow`, `__builtin_mul_overflow`). They exist in GCC and clang and compute the true result in a wider type, so there is no case the programmer must remember. The portable alternative for unsigned values is to test before multiplying: `b != 0 && a > SIZE_MAX / b`.
- `array_alloc(n, size)` is `malloc(n * size)` with the multiplication checked. The C library has the same thing under the name `reallocarray` (glibc 2.26 and later, and the BSDs); a call with `p == NULL` is an allocation. Section 7 measures both.
- `str_copy` and `str_cat` have the semantics of BSD `strlcpy` and `strlcat`: they never write past `dst[size-1]`, they always terminate when `size > 0`, and they return the length they **tried** to make. A caller that wants to know about truncation writes `if (str_copy(dst, sizeof dst, src) >= sizeof dst)`.

**Why not `strncpy`?** It is often recommended, and it is the wrong tool. `strncpy(dst, src, n)` does not terminate `dst` when `src` has `n` or more characters, and it writes `n` bytes every time (it pads the rest with zeros), which is slow for large buffers and hides the missing terminator in the common case where the input is short. Problem B has the bug. `strncat` is correct but its size argument is the room for the **appended** characters, not the size of the buffer, which is the usual source of off-by-one errors. `strlcpy` and `strlcat` are in BSD libc and, from memory, in glibc since 2.38; the helpers above are written out so that they work on any C library, and `str_cat` uses `strnlen` so that a destination that is not terminated inside its buffer is detected and left alone instead of read past its end.

**The loop invariant for parsing.** The rule that makes parsers safe is to track the room left, not the end pointer. If `pos <= n` is an invariant, then `n - pos` cannot wrap, and "do I have `k` more bytes" is the test `n - pos >= k`. The tempting form `pos + k <= n` wraps when `k` is huge (a 32 bit length added to a 32 bit position) and then the check passes. The parser in the next section uses `n - pos` everywhere.

---

## 2. A parser with a first draft and a second draft

The message format is small and typical: the bytes `MSG1`, a version byte (1), a count (16 bit little endian), then `count` records of one type byte, a 16 bit length and that many bytes of data. The message must end exactly after the last record.

```c
/* A small binary message format:  "MSG1", version 1, count (u16 little endian),
   then count records of: type (u8), len (u16 little endian), len bytes of data.
   The message must end exactly after the last record. */
#define REC_MAX 64
#define MSG_MAX_RECS 1000

typedef struct {
    uint8_t type;
    uint16_t len;
    uint8_t data[REC_MAX];
} Rec;

typedef struct {
    uint16_t count;
    Rec *recs;
} Msg;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static void msg_free(Msg *m)
{
    if (!m)
        return;
    free(m->recs);
    free(m);
}

/* Version 1: it parsed every message its author tried. */
static Msg *parse_v1(const uint8_t *buf, size_t n)
{
    if (n < 7 || memcmp(buf, "MSG1", 4) != 0 || buf[4] != 1)
        return NULL;
    Msg *m = malloc(sizeof *m);
    m->count = rd16(buf + 5);
    m->recs = calloc(m->count ? m->count : 1, sizeof(Rec));
    size_t pos = 7;
    for (uint16_t i = 0; i < m->count; ++i) {
        m->recs[i].type = buf[pos];
        m->recs[i].len = rd16(buf + pos + 1);
        memcpy(m->recs[i].data, buf + pos + 3, m->recs[i].len);
        pos += 3 + m->recs[i].len;
    }
    return m;
}

/* Version 2: every length is checked against the buffer before it is used. */
static Msg *parse_v2(const uint8_t *buf, size_t n)
{
    if (n < 7 || memcmp(buf, "MSG1", 4) != 0 || buf[4] != 1)
        return NULL;
    uint16_t count = rd16(buf + 5);
    if (count > MSG_MAX_RECS)
        return NULL;
    Msg *m = calloc(1, sizeof *m);
    if (!m)
        return NULL;
    m->recs = calloc(count ? count : 1, sizeof(Rec));
    if (!m->recs) {
        free(m);
        return NULL;
    }
    m->count = count;
    size_t pos = 7;                                   /* invariant: pos <= n */
    for (uint16_t i = 0; i < count; ++i) {
        if (n - pos < 3)
            goto bad;
        uint16_t len = rd16(buf + pos + 1);
        if (len > REC_MAX || n - pos - 3 < len)
            goto bad;
        m->recs[i].type = buf[pos];
        m->recs[i].len = len;
        memcpy(m->recs[i].data, buf + pos + 3, len);
        pos += 3 + (size_t)len;
    }
    if (pos != n)                                     /* trailing bytes are an error */
        goto bad;
    return m;
bad:
    msg_free(m);
    return NULL;
}
```

`parse_v1` is what a first draft looks like. It works on every message its author tried, and it has five defects:

1. it reads the count and does not limit it (65,535 records of about 68 bytes each is a 4.4 MB allocation, from a 7 byte message);
2. it reads the record header (`buf[pos]`, `rd16(buf + pos + 1)`) without checking that three bytes are left;
3. it copies `len` bytes into a 64 byte field without checking `len <= 64`: a heap overflow with an attacker-chosen size;
4. it reads `len` bytes from the input without checking that they exist: an over-read, which leaks heap memory or crashes;
5. it never checks that `malloc` and `calloc` succeeded, and it never checks that the message ends where it should.

`parse_v2` fixes all five with the same two comparisons: `n - pos < 3` and `len > REC_MAX || n - pos - 3 < len`. Notice `goto bad` (C 10, section 3): there is one place that frees the half-built message, so a failure in record 500 does not leak the 499 before it.

---

## 3. Testing: plain C, an oracle, mutants, coverage

### A unit test framework in eight lines

You do not need a framework to start. This is the whole thing, and it is the shape of Unity, CMocka and the others (they add fixtures, mocks and nicer output):

```c
#include <stdio.h>
#include <string.h>
#include "../code/sec.h"

/* A unit test framework in eight lines: count, report the first failing line, exit non-zero. */
static int run, failed;
#define EXPECT(cond) do { run++; if (!(cond)) { failed++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define TEST(name) static void name(void)

TEST(test_str_copy)
{
    char buf[8];
    EXPECT(str_copy(buf, sizeof buf, "abc") == 3 && strcmp(buf, "abc") == 0);
    EXPECT(str_copy(buf, sizeof buf, "0123456789") == 10 && strcmp(buf, "0123456") == 0);
}

TEST(test_str_cat)
{
    char buf[8] = "ab";
    EXPECT(str_cat(buf, sizeof buf, "cd") == 4 && strcmp(buf, "abcd") == 0);
}

TEST(test_size_mul)
{
    size_t out;
    EXPECT(size_mul(3, 4, &out) == 0 && out == 12);
    EXPECT(size_mul(SIZE_MAX, 2, &out) == -1);
}

static const uint8_t good[] = {'M', 'S', 'G', '1', 1, 2, 0, 1, 3, 0, 'a', 'b', 'c', 2, 1, 0, 'z'};

TEST(test_parse_valid)
{
    Msg *m = parse_v2(good, sizeof good);
    EXPECT(m != NULL && m->count == 2);
    EXPECT(m && m->recs[0].len == 3 && memcmp(m->recs[0].data, "abc", 3) == 0);
    EXPECT(m && m->recs[1].type == 2 && m->recs[1].data[0] == 'z');
    msg_free(m);
}

TEST(test_parse_bad_magic)
{
    uint8_t bad[sizeof good];
    memcpy(bad, good, sizeof good);
    bad[0] = 'X';
    EXPECT(parse_v2(bad, sizeof bad) == NULL);
}

TEST(test_parse_truncated)
{
    EXPECT(parse_v2(good, sizeof good - 1) == NULL);
}

int main(void)
{
    test_str_copy();
    test_str_cat();
    test_size_mul();
    test_parse_valid();
    test_parse_bad_magic();
    test_parse_truncated();
    printf("%d checks, %d failed\n", run, failed);
    return failed != 0;
}
```

Output:

```text
10 checks, 0 failed
```

Count, report the first failing line with `__FILE__` and `__LINE__`, return non-zero so that `make test` and CI fail. Unity and CMocka (the two most used in embedded work) give you `TEST_ASSERT_EQUAL_INT`, `setUp` and `tearDown`, and CMocka adds mock functions so that a driver can be tested without the hardware. I did not install them for this tutorial; the macro above is the part that matters.

### Tests that compare against an independent oracle

Hand-written examples (the ten checks above) find the bugs you thought of. The better test is one where a **second, independently written implementation** tells you the right answer for thousands of random inputs. For the parser the oracle is `ref_parse`, written in a different style (a "bytes left" counter instead of a position, plain `unsigned` arithmetic instead of `size_t`). A test program generates random valid messages (with the count at 0, at the limit of 1000, just over it, and with records at 64 and 65 bytes) and nine kinds of corruption (truncate, flip a byte, bad magic, bad version, count too large, length too large, drop a record, append junk, empty), then:

- checks `parse_v2` and the oracle agree on accept or reject, and on every field of an accepted message;
- copies each input into a **heap buffer of exactly its size** before parsing, so that AddressSanitizer sees any read one byte past the end (a stack array or a big buffer would hide it);
- checks that `parse_v1` agrees with the oracle on valid input only (it is allowed to fail on corrupt input; the point is that the test knows).

The same program tests `size_add`, `size_mul` against `unsigned __int128` arithmetic (edge values around `SIZE_MAX` and `2^32`), `array_alloc`, and `str_copy` and `str_cat` against a reference that works on lengths. At the default 100,000 iterations it makes **13,805,215 checks** and passes under `-fsanitize=address,undefined` with no sanitizer report. A passing count is not evidence by itself: the next step asks whether these checks can fail.

### Mutants: do the tests notice a bug?

A mutant is the library with one deliberate defect. If the tests still pass, they cannot see that defect, and a real bug of the same shape would also pass. I made **24 deliberate defects**: arithmetic that ignores overflow, a copy that writes one byte too many, `strcat` that uses `strlen` on an unterminated buffer, a parser that checks only three magic bytes, one that ignores the version, one that accepts a header one byte short, one that rejects exactly the maximum count or the maximum record length, and so on. Each was compiled in and the test program run:

```text
caught size_add ignores overflow check
caught size_mul ignores overflow sanitizer/crash
caught array_alloc multiplies without checking sanitizer/crash
ESCAPED array_alloc returns NULL for a zero size
caught str_copy writes one byte too many sanitizer/crash
caught str_copy returns the copied length, hiding truncation check
caught str_cat on an unterminated buffer check
caught str_cat uses strlen on a buffer that may be unterminated sanitizer/crash
caught str_cat returns only the appended length check
caught parse_v2 checks only 3 magic bytes check
caught parse_v2 ignores the version check
caught parse_v2 accepts a header one byte short sanitizer/crash
caught parse_v2 rejects exactly the maximum count check
caught parse_v2 has no limit on the count check
caught parse_v2 reads a record header one byte short sanitizer/crash
caught parse_v2 rejects a record of exactly the maximum length check
caught parse_v2 does not limit the record length sanitizer/crash
caught parse_v2 rejects a record that ends exactly at the buffer end check
caught parse_v2 does not check the record against the buffer sanitizer/crash
caught parse_v2 accepts trailing bytes check
caught parse_v2 leaks the message on a bad input sanitizer/crash
caught rd16 reads big endian sanitizer/crash
caught parse_v2 advances one byte too little check
caught parse_v2 stores the wrong count check
23 of 24
```

**23 were caught**: 13 by a failing check and 10 by a sanitizer report or a crash. The 10 are the important ones for this chapter: without the exact-size heap copies and ASan, those defects (a short read, a missing bounds check) would have passed every check. **One escaped**: "array_alloc returns NULL for a zero size". On glibc, `malloc(0)` returns a non-NULL pointer, so `array_alloc(0, 8)` and `malloc(0)` behave the same and the mutant is equivalent here; on a C library where `malloc(0)` returns NULL the helper's `total ? total : 1` is what keeps the behaviour portable. I left that defect in the table instead of removing it.

Mutation testing is how you find the weak tests. On my first run several defects escaped (a header one byte short, a record header one byte short, an unchecked length, no length limit, no count limit). They were fixed by the exact-size heap copy and two more build modes, and the table above is the second run.

### Coverage with gcov

`gcc --coverage -O0` instruments the program; running it writes `.gcda` files and `gcov -f -b` prints the lines and branches that ran. I measured two test sets against the same parser, my ten unit checks and the files the fuzzer saved in its corpus (20 files: 18 of its own and 2 seeds), each replayed through `parse_v2`:

```text
gcov, parse_v2 only (31 lines)      lines run
unit tests (10 checks)              80.65%
fuzz corpus replay (20 files)       90.32%

gcov, whole header (69 lines, 48 branches)   lines run   branches taken at least once
unit tests (10 checks)                       60.87%      41.67%
fuzz corpus replay (20 files)                49.28%      50.00%
```

Three observations. The fuzz corpus covers more of `parse_v2` (90.32 percent of its 31 lines) than my ten checks (80.65 percent), and it misses only the three lines that handle `calloc` returning NULL. The unit tests miss more: the count limit, a short header, a bad length and the allocation failures. And the "whole header" numbers are lower for the corpus replay on lines (49.28 percent) because the header also holds the helpers and `parse_v1`, which the replay never calls. Coverage says what ran; it does not say what was checked, and a test that runs a line without asserting anything counts the same. The error paths that need a failing allocator are tested with the injected failing `malloc` of C 09 and C 10, or with a hand-written test.

---

## 4. Fuzzing

A fuzzer generates inputs, runs your code and watches for a crash or a sanitizer report. A coverage-guided fuzzer keeps the inputs that reach new code and mutates them, so it learns the format from the program. You write a **harness**: a function that takes bytes and calls the code under test.

### libFuzzer

libFuzzer is in clang. The harness is a function with a fixed name; `-fsanitize=fuzzer` supplies `main`:

```c
#include <stddef.h>
#include <stdint.h>
#include "../code/sec.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    Msg *m = parse_v1(data, size);
    msg_free(m);
    return 0;
}
```

Build with `clang -g -O1 -fsanitize=fuzzer,address fuzz_v1.c -o fuzz_v1` and run `./fuzz_v1 corpus_dir`. The first run, on the buggy `parse_v1`, with an empty corpus:

```text
$ clang -g -O1 -fsanitize=fuzzer,address fuzz_v1.c -o fuzz_v1 && ./fuzz_v1 -seed=1 c1
INFO: Seed: 1
#2 INITED cov: 3 ft: 3 corp: 1/1b
#436 NEW cov: 4 ft: 4 corp: 2/9b lim: 8
ERROR: AddressSanitizer: heap-buffer-overflow on address ADDR at pc ADDR bp ADDR sp ADDR
READ of size 1 at ADDR thread T0
 #1 parse_v1 sec.h:88:26
 #2 LLVMFuzzerTestOneInput fuzz_v1.c:7:14
SUMMARY: AddressSanitizer: heap-buffer-overflow sec.h:67:68 in rd16
artifact_prefix='./'; Test unit written to ./crash-2430afe6030445ccb343a7f28e30ac1d32313099
```

It found a heap overflow after a few hundred executions (the last line of new coverage is execution 436), which took about a tenth of a second. The saved input is 9 bytes:

```text
the input the fuzzer saved (9 bytes):
 4d 53 47 31 01 01 32 00 00
```

That is `MSG1`, version 1, a count of 0x3201 (12,801), and then the type byte of the first record with only one byte left where the two byte length should be: the parser trusts the count and reads the length one byte past the end of the 9 byte buffer. Across three seeds and with or without the two seed files, the crash always appeared within 0.1 s, in two different places (the `rd16` of a record header, and the `memcpy` of `len` bytes, which a different seed reported as a read of 8,548 bytes). Run in this container, on the repaired parser, for 30 seconds:

```text
$ ./fuzz_v2 -seed=1 -max_total_time=30 -max_len=200 c2 seeds
#18339020 DONE   cov: 20 ft: 32 corp: 16/317b lim: 200 exec/s: 591581
Done 18339020 runs in 31 second(s)
```

The fuzzer ran about 18 million inputs with no crash (an earlier 60 second run did 34.8 million). That is evidence about the inputs it tried, not a proof, and it only says "no crash and no sanitizer report". It cannot say "the answer is right". That needs an oracle.

### A differential fuzzer: the oracle inside the harness

Put the reference parser from section 3 into the harness and compare:

```c
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifndef HDR
#define HDR "../code/sec.h"
#endif
#include HDR
#include "ref.h"

static Ref ref;

/* The fuzzer is the test generator and ref_parse is the oracle: any disagreement is a bug. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    ref_parse(data, size, &ref);
    Msg *m = parse_v2(data, size);
    if ((m != NULL) != ref.ok)
        __builtin_trap();
    if (m) {
        if (m->count != ref.count)
            __builtin_trap();
        for (unsigned i = 0; i < ref.count; ++i)
            if (m->recs[i].type != ref.r[i].type || m->recs[i].len != ref.r[i].len || memcmp(m->recs[i].data, ref.r[i].data, ref.r[i].len) != 0)
                __builtin_trap();
    }
    msg_free(m);
    return 0;
}
```

with the reference parser in a header:

```c
#ifndef HDR
#define HDR "../code/sec.h"
#endif
#include HDR
typedef struct { int ok; unsigned count; struct { int type; unsigned len; uint8_t data[REC_MAX]; } r[MSG_MAX_RECS]; } Ref;

static void ref_parse(const uint8_t *b, size_t n, Ref *out)
{
    out->ok = 0;
    if (n < 7 || b[0] != 'M' || b[1] != 'S' || b[2] != 'G' || b[3] != '1' || b[4] != 1)
        return;
    unsigned count = b[5] + 256u * b[6];
    if (count > MSG_MAX_RECS)
        return;
    size_t left = n - 7;
    size_t at = 7;
    for (unsigned i = 0; i < count; ++i) {
        if (left < 3)
            return;
        unsigned len = b[at + 1] + 256u * b[at + 2];
        if (len > REC_MAX || left - 3 < len)
            return;
        out->r[i].type = b[at];
        out->r[i].len = len;
        memcpy(out->r[i].data, b + at + 3, len);
        at += 3 + len;
        left -= 3 + len;
    }
    if (left != 0)
        return;
    out->ok = 1;
    out->count = count;
}
```

`__builtin_trap()` turns a disagreement into a crash that the fuzzer reports and minimises like any other. On the correct parser, 30 seconds:

```text
$ ./fuzz_diff -seed=1 -max_total_time=30 -max_len=200 c3 seeds
#11836923 DONE   cov: 41 ft: 71 corp: 27/796b lim: 200 exec/s: 381836
Done 11836923 runs in 31 second(s)
```

No disagreement in about 11.8 million inputs. Now the test of the test: I put five of the section 3 defects back into `parse_v2` one at a time and asked the differential fuzzer to find each, with three random seeds (time to the first crash):

```text
parse_v2 rejects a record of exactly the maximum length      15434 ms / 4265 ms / 3179 ms
parse_v2 accepts trailing bytes                              96 ms / 91 ms / 90 ms
parse_v2 checks only 3 magic bytes                           91 ms / 81 ms / 86 ms
parse_v2 rejects exactly the maximum count                   not found in 30 s / not found in 30 s / not found in 30 s
parse_v2 advances one byte too little                        84 ms / 80 ms / 81 ms
```

Three of the five are found in under 0.1 s. The record-length limit took 3 to 15 seconds because the fuzzer must build a record of exactly 64 bytes (the `len >= 64` mutant rejects 64 and accepts 63). The last one, "rejects exactly the maximum count", was **not found in 30 seconds by any of the three seeds**. To trigger it the fuzzer must produce a valid message of exactly 1000 records, which is at least 7 + 1000 × 3 = 3,007 bytes, and I limited inputs to 200 bytes (`-max_len=200`) to keep the other runs fast. That is the honest result and the general lesson: a coverage-guided fuzzer finds the bugs that are one or two mutations away from an input it already has, and a bug behind a count or a checksum that needs a large exact input must be fed by a seed, a larger `-max_len`, or a hand-written test. The unit test suite does catch that mutant (section 3). The two methods cover different holes.

### AFL++

AFL++ is the other common fuzzer (a maintained fork of AFL). Its compiler wrapper `afl-cc` instruments the target, and `afl-fuzz` reads from a seed directory and writes to an output directory. It gives the program a file or standard input; the main for that is:

```c
#include <stdint.h>
#include <stdio.h>
#include "../code/sec.h"

int main(void)
{
    static uint8_t buf[1 << 16];
    size_t n = fread(buf, 1, sizeof buf, stdin);
    Msg *m = PARSE(buf, n);
    msg_free(m);
    return 0;
}
```

I built it with `afl-cc` and `AFL_USE_ASAN=1`, with `-DPARSE=parse_v1`, and ran 30 seconds from the two seed files. The statistics file at the end:

```text
$ afl-fuzz -i seeds -o aout -V 30 -- ./afl_v1   (afl_v1 = parse_v1 built with afl-cc and AFL_USE_ASAN=1)
execs_done: 30176
execs_per_sec: 1005.26
corpus_count: 12
bitmap_cvg: 56.25%
saved_crashes: 2
```

AFL++ found a crash, in the run before this one after 39 executions and 20 ms, and saved 2 crashing inputs in 30 seconds. It ran about 1,000 executions per second against about 570,000 for libFuzzer on the same machine. The comparison is unfair to AFL++: it ran with ASan, I did not enable its persistent mode, and the machine has 2 cores, so I draw no conclusion about the tools. Use libFuzzer when you can compile the code with clang and the target is a library function; use AFL++ when the target is a whole program that reads a file or a socket, when you cannot change `main`, or when you need to fuzz a binary without source (QEMU mode, which I did not test).

### Harness pitfalls (measured)

My first harness for problem D (section 12) looked correct and found nothing in 20 seconds and 26 million runs, because it called the parser and threw away the result. At `-O1` the optimiser deleted the call, since the function has no side effect. The fuzzer prints this if you read it:

```text
$ ./pd_old -seed=1 -max_len=64 -max_total_time=20 pdo   (result of kv_value_len unused)
INFO: Loaded 1 PC tables (1 PCs): 1
#2 INITED cov: 1 ft: 1 corp: 1/1b
Done 26446092 runs in 21 second(s)
```

`1 PCs`, `cov: 1`: the only instrumented point is the harness itself. The harness with the result kept in a `volatile` finds the bug at once (problem D). Other pitfalls that waste fuzzing time: a harness that uses global state and leaks between runs, an `exit()` or `abort()` in the target on bad input (the fuzzer reports a crash for every input), a target that reads the clock or a random number (not reproducible), and no corpus of valid inputs for a format with a checksum or a magic number.

**Fuzzing as regression.** Keep the corpus and every crash input in the repository and replay them as a test (`replay.c` in the sources above does that: it reads each file and calls `parse_v2`). Then every bug the fuzzer ever found stays fixed.

---

## 5. Static analysis: what each tool catches

Static tools read the source without running it. I planted 12 defects (a leak on an early return, a NULL dereference after `malloc`, use after free, double free, a constant index past the end, an unbounded `strcpy`, an uninitialised variable, a format string from the caller, the address of a local returned, an `int` overflow in a size passed to `malloc`, a loop off by one, a `FILE` that is never closed), one per function, and wrote a corrected twin of each. A script ran every tool and credited it with a defect only when a diagnostic fell inside the defective function. The dynamic columns run the program with the input that triggers each defect.

```text
defect                          | gcc -Wall -Wextra -O2 | gcc -fanalyzer | clang --analyze | clang-tidy | cppcheck | ASan+UBSan+LSan run | MSan run
B1  leak on early return        |           no          |      yes       |       yes       |    yes     |   yes    |         yes         |    no
B2  NULL after malloc           |           no          |       no       |        no       |     no     |   yes    |          no         |    no
B3  use after free              |          yes          |      yes       |       yes       |    yes     |   yes    |         yes         |   yes
B4  double free                 |          yes          |      yes       |       yes       |    yes     |   yes    |         yes         |    no
B5  constant index past end     |          yes          |      yes       |       yes       |    yes     |   yes    |         yes         |    no
B6  unbounded strcpy            |           no          |       no       |        no       |    yes     |    no    |         yes         |   yes
B7  uninitialised variable      |          yes          |      yes       |       yes       |    yes     |   yes    |          no         |   yes
B8  format string               |          yes          |      yes       |        no       |    yes     |    no    |          no         |    no
B9  address of a local          |          yes          |      yes       |       yes       |    yes     |   yes    |         yes         |    no
B10 int overflow in malloc size |           no          |       no       |        no       |    yes     |    no    |         yes         |    no
B11 loop off by one             |          yes          |      yes       |        no       |     no     |   yes    |         yes         |    no
B12 FILE never closed           |           no          |      yes       |        no       |     no     |   yes    |          no         |    no
found out of 12 |           7           |       9        |        6        |     9      |    9     |          8          |    3
diagnostics on the 12 corrected twins: gcc -Wall -Wextra -O2 1, gcc -fanalyzer 0, clang --analyze 0, clang-tidy 4, cppcheck 2
```

Read the matrix this way:

- **No tool finds everything**, and the best static tools found 9 of 12. The union is 12, so the tools overlap and complement each other: B2 (NULL after a `malloc` that is not checked) is found only by `cppcheck`; B12 (the `FILE` never closed) only by `-fanalyzer` and `cppcheck`; B10 (the `int` overflow that feeds `malloc`) only by `clang-tidy` and, at `-O0`, UBSan.
- **GCC's `-fanalyzer`** (which looks for paths through the code: leaks, double free, use after free, null dereference, uninitialised values) found 9 and reported **nothing** on the 12 corrected twins. It is the quietest of the tools in this measurement. It supports C (the manual says so for this release) and only works when the whole path is inside one translation unit.
- **`clang-tidy`** found 9 but gave 4 diagnostics on the corrected code. Looking at them, they are policy checks and not bugs: `cert-err33-c` (the return value of `snprintf` is ignored), the `insecureAPI` check on any use of `strcpy` (including a safe one), a `malloc(0)` warning. Policy checks are useful (they are the CERT C rules in section 9), and they need to be configured to the rules your project follows, or they bury the real warning.
- **`cppcheck`** found 9 and gave 2 diagnostics on the corrected code (style: a write-only buffer and a const array).
- **`gcc -Wall -Wextra -O2`** found 7 with 1 diagnostic on the corrected code. These are warnings you already have. They also depend on the optimisation level: some analyses run only with `-O2`.
- **Run-time**: ASan, UBSan and LSan together found 8; MSan found 3 (it is a clang tool, and I ran it at `-O0`: at `-O1` the compiler had folded the uninitialised read away). The combined run misses B2 (in the test `malloc` never fails, so the NULL is never used), B7 (that is MSan's job), B8 (a leak of values is not a memory error) and B12 (LSan cannot see the `FILE`, because libc keeps the open stream reachable from its own list). A sanitizer result also depends on the build: UBSan found B10 at `-O0` but **did not report it at `-O1` with GCC** (measured).

**What to do with it.** Run `-Wall -Wextra -Wformat=2 -Wshadow -Wconversion` (with `-Werror` in CI), `-fanalyzer` and `clang-tidy` with the CERT checks (`clang-tidy -checks='cert-*,clang-analyzer-*,bugprone-*'`) on every build, `cppcheck` as a second opinion, and the sanitizers in the tests. The cost is a few minutes of CI; each tool finds something another misses.

---

## 6. Memory-safety attacks and the defences

### Stack smashing and the canary

A local array sits next to saved registers and the return address. `strcpy` into a 16 byte array from a longer string overwrites what follows it. This program does exactly that (`fflush` makes the first line visible before the crash):

```c
#include <stdio.h>
#include <string.h>

__attribute__((noinline)) static void greet(const char *name)
{
    char buf[16];
    strcpy(buf, name);                 /* no length check */
    printf("hello, %s\n", buf);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    greet(argc > 1 ? argv[1] : "world");
    puts("greet returned normally");
    return 0;
}
```

Run with 40 `A`s as the argument, built with four different flag sets:

```text
-- all protections off:
hello, AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
exit status 139
-- stack protector only (-O0, fortify off):
hello, AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
*** stack smashing detected ***: terminated
exit status 134
-- fortify only (-O2, no stack protector):
*** buffer overflow detected ***: terminated
exit status 134
-- Ubuntu default flags (gcc -O2):
*** buffer overflow detected ***: terminated
exit status 134
```

- **All protections off**: `hello, AAAA...` is printed, and then the program crashes with exit status 139 (SIGSEGV), most likely because the return address was overwritten by `AAAA...` and `ret` jumped to an address that does not exist (I did not look at the registers). A real attacker puts a chosen address there instead.
- **Stack protector** (`-fstack-protector-strong`): the compiler puts a random **canary** value between the local arrays and the saved return address and checks it before `ret`. The overwritten canary is detected: `*** stack smashing detected ***: terminated`, exit status 134 (SIGABRT). The program stops before the damage is used, but only **after** the overflow happened. (From memory: on x86-64 glibc the canary lives in thread-local storage and its lowest byte is zero, so that a string copy cannot write it. I did not measure that.)
- **`_FORTIFY_SOURCE`** (with `-O2`): the compiler replaces `strcpy(buf, name)` by `__strcpy_chk(buf, name, sizeof buf)` because it knows `buf` is 16 bytes. The overflow is stopped **at the call**, before any byte is written past the end: `*** buffer overflow detected ***: terminated`. This is why the Ubuntu default build (the last case) reports the fortify message and not the canary message: the stronger check fired first. It only works when the compiler knows the size of the destination.
- **AddressSanitizer** gives the best report and is for testing, not production:

```text
ERROR: AddressSanitizer: stack-buffer-overflow on address ADDR at pc ADDR bp ADDR sp ADDR
WRITE of size 41 at ADDR thread T0
    #1 greet d1_smash.c:7
    #2 main d1_smash.c:14
    #3 __libc_start_call_main libc_start_call_main.h:58
    #4 __libc_start_main_impl ../csu/libc-start.c:360
Address ADDR is located in stack of thread T0 at offset 48 in frame
    #0 greet d1_smash.c:5
SUMMARY: AddressSanitizer: stack-buffer-overflow asan_interceptors.cpp:563 in strcpy
```

It names the line (`d1_smash.c:7`), says it is a write of 41 bytes (40 characters and the terminator), and says where in the frame the variable lives. Use it in every test and fuzz run.

### Format-string bugs

`printf(argv[1])` hands the user's text to the formatter as the format. `%x` prints the next value from where the arguments would be, `%s` follows a pointer, and `%n` **writes** the count of characters printed so far to an address.

```c
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc > 1)
        printf(argv[1]);               /* the user controls the format string */
    printf("\n");
    return 0;
}
```

The compiler warns when the format is not a literal and has no arguments:

```text
warning: format not a string literal and no format arguments [-Wformat-security]
```

and the program leaks stack and register values:

```text
$ ./d2 "%x.%x.%x.%x"
79da33f8.79da3410.e0968db8.0
```

The repair is a constant format: `printf("%s", argv[1])`. Defences: `-Wformat-security` (on by default on Ubuntu), `-Werror=format-security`, and `_FORTIFY_SOURCE`, which rejects `%n` in a format string that is in writable memory:

```text
$ ./d2f "%n"   (built with -O2 -D_FORTIFY_SOURCE=2)
*** %n in writable segment detected ***
exit status 134
```

Every logging or error function you write that takes a format should be declared with `__attribute__((format(printf, n, m)))` (C 10, section 1) so that the compiler checks its callers.

### ASLR, NX, PIE, RELRO, CET: the platform's part

Exploits need addresses and executable memory. The platform takes both away:

- **NX** (no execute): the stack and heap are not executable, so injected code does not run. It is a flag in the ELF header (`GNU_STACK` is `RW`, not `RWE`) and a hardware bit in the page tables.
- **ASLR**: the kernel places the stack, heap, libraries and a position independent executable at random addresses on each run. This program prints four addresses:

```c
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    int local = 0;
    void *heap = malloc(16);
    printf("stack %p  heap %p  libc printf %p  main %p\n", (void *)&local, heap, (void *)printf, (void *)main);
    free(heap);
    return 0;
}
```

```text
two normal runs:
stack 0x7ffda9bb14dc  heap 0x55a93cebb2a0  libc printf 0x7fa8db460100  main 0x55a90aa92199
stack 0x7ffcaf84d14c  heap 0x561fae0b62a0  libc printf 0x7fb0f2c60100  main 0x561f6fad5199
two runs with randomisation switched off (setarch -R):
stack 0x7fffffff623c  heap 0x5555555592a0  libc printf 0x7ffff7c60100  main 0x555555555199
stack 0x7fffffff623c  heap 0x5555555592a0  libc printf 0x7ffff7c60100  main 0x555555555199
```

Two normal runs give different addresses for all four. With randomisation switched off (`setarch x86_64 -R`) the two runs are identical, and the addresses are the familiar `0x7fff...` and `0x5555...`. The kernel setting here is `/proc/sys/kernel/randomize_va_space = 2` (full). Look at the low digits: `main` ends in `199` and `printf` in `100` in every run, because the kernel randomises whole pages (the low 12 bits stay). An attacker who learns **one** address (a leak from a format-string bug, for example) can compute the rest of that module: ASLR is a defence against blind guessing and fails when there is an information leak, which is why the format-string leak above matters.
- **PIE** (position independent executable) makes the program's own code randomisable too. Without it, `main` is at a fixed address and gadgets in the program are always where the attacker expects.
- **RELRO** (relocation read-only): with `-Wl,-z,relro,-z,now` the GOT (the table of function addresses) is resolved at startup and made read-only, so an overwrite of a GOT entry no longer redirects a call.
- **CET** (control-flow enforcement, `-fcf-protection=full`): end-branch markers (IBT) restrict indirect jumps to valid targets, and a hardware shadow stack (needs kernel and C library support; from memory, not measured here) keeps a second copy of return addresses.

You can read all of these from a binary. I built one program three ways and checked the result with `readelf` and `nm`:

```text
build      | PIE        | NX stack | RELRO     | canary    | fortified    | CET (IBT)
off        | no         | NO       | none      | no        | no           | no
default    | yes        | yes      | full      | yes       | yes          | yes
hardened   | yes        | yes      | full      | yes       | yes          | yes
```

The "off" build (no canary, no fortify, no PIE, executable stack, no RELRO, no CET) has no protection at all. The Ubuntu default of GCC 13.3 has **all of them already**, and my explicit "hardened" flag set (`-fstack-protector-strong -D_FORTIFY_SOURCE=3 -fPIE -pie -Wl,-z,relro,-z,now -fstack-clash-protection -fcf-protection=full`) gives the same row. If you build on Ubuntu or Fedora you have these defaults; if you build for an embedded target with a bare-metal or minimal toolchain, you probably do not, and you must ask for each. Tools such as `checksec` print the same table; I read it with `readelf` to show where each bit comes from.

---

## 7. Integer overflow in size calculations

The most common route from "a number in a header" to "heap overflow" is a size computed in too small a type. The program has a count that an attacker controls and a record of 16 bytes:

```c
#define _DEFAULT_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../code/sec.h"

typedef struct { double a, b; } Item;           /* 16 bytes */

int main(int argc, char **argv)
{
    (void)argv;
    uint32_t count = 0x10000001u;               /* what an attacker puts in a header */
    if (argc > 1) {
        uint32_t bytes = count * 16u;               /* 32 bit arithmetic: wraps to 16 */
        Item *v = malloc(bytes);
        printf("allocated %u bytes for %u items\n", (unsigned)bytes, count);
        for (uint32_t i = 0; i < count; ++i)
            v[i].a = v[i].b = 0;                /* writes far past the allocation */
        free(v);
        return 0;
    }
    volatile size_t big = (size_t)1 << 62;      /* an item count read from a header */
    errno = 0;
    void *p = array_alloc(big, 16);
    printf("array_alloc(2^62, 16): %s, errno = %d (%s)\n", p ? "allocated" : "NULL", errno, strerror(errno));
    errno = 0;
    p = reallocarray(NULL, big, 16);
    printf("reallocarray(NULL, 2^62, 16): %s, errno = %d (%s)\n", p ? "allocated" : "NULL", errno, strerror(errno));
    size_t t;
    printf("size_mul(2^32, 2^32) reports overflow: %s\n", size_mul((size_t)1 << 32, (size_t)1 << 32, &t) ? "yes" : "no");
    return 0;
}
```

With an argument, the 32 bit multiplication `0x10000001 * 16` wraps to 16 (the result is `0x100000010`, and only the low 32 bits survive), `malloc` gives 16 bytes, and the loop then starts writing 268 million records into it (ASan stops it at the first write past the end):

```text
ERROR: AddressSanitizer: heap-buffer-overflow on address ADDR at pc ADDR bp ADDR sp ADDR
WRITE of size 8 at ADDR thread T0
    #0 main d5_overflow.c:19
    #1 __libc_start_call_main libc_start_call_main.h:58
    #2 __libc_start_main_impl ../csu/libc-start.c:360
ADDR is located 8 bytes after 16-byte region [ADDR,ADDR)
    #1 main d5_overflow.c:16
    #2 __libc_start_call_main libc_start_call_main.h:58
    #3 __libc_start_main_impl ../csu/libc-start.c:360
SUMMARY: AddressSanitizer: heap-buffer-overflow d5_overflow.c:19 in main
```

ASan says a write of 8 bytes, "located 8 bytes after 16-byte region", allocated at line 16: the allocation was right for the wrapped size and wrong for the intent. With `size_t` (64 bit) the count must be larger, but the bug is the same: any `count * size` computed in a type that is too narrow, or in `int`, or not checked, wraps. Without an argument the program shows the repairs:

```text
array_alloc(2^62, 16): NULL, errno = 12 (Cannot allocate memory)
reallocarray(NULL, 2^62, 16): NULL, errno = 12 (Cannot allocate memory)
size_mul(2^32, 2^32) reports overflow: yes
```

`array_alloc` and `reallocarray` both return NULL with `errno == ENOMEM` for a product that does not fit in `size_t`, and `size_mul` reports the overflow itself. The rule: **every `malloc` whose size is computed from untrusted numbers goes through a checked multiplication and addition** (`reallocarray`, `size_mul`, `calloc` for arrays: `calloc(n, size)` checks the product too). Also check the type of the arithmetic before it is converted: `size_t total = a * b` multiplies in the type of `a` and `b`, then converts.

Two related traps. **Signed overflow is undefined behaviour**, so `if (a + b < a)` as an overflow test can be deleted by the optimiser; use the builtins. **Comparison of signed and unsigned**: `if (len < 0 || len > max)` for a `size_t len` is never true on the first half, and a negative `int` converted to `size_t` becomes a huge number (`-Wsign-compare`, `-Wconversion`).

---

## 8. TOCTOU: time of check, time of use

A TOCTOU bug is a check on a **name** followed by a use of the name: between the two, an attacker (or another process) changes what the name means. `access(path, R_OK)` followed by `open(path)` is the classic. The man page for `access(2)` says in so many words that this pattern is a security hole, because the file can change between the two calls, and that it should be avoided. The program below does both versions. The "attacker" is the same process, which swaps a symbolic link into place in the window, so that the result is the same every time:

```c
#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    fputs(text, f);
    fclose(f);
}

/* The attacker's move, done by the same process so the demo is deterministic. */
static void swap_in_symlink(const char *path)
{
    unlink(path);
    symlink("secret.txt", path);
}

static void show(const char *label, int fd)
{
    char buf[32] = {0};
    if (fd < 0) {
        printf("%s: open failed: %s\n", label, strerror(errno));
        return;
    }
    ssize_t n = read(fd, buf, sizeof buf - 1);
    printf("%s: read \"%s\"\n", label, n > 0 ? buf : "");
    close(fd);
}

int main(void)
{
    char dir[] = "/tmp/toctouXXXXXX";
    if (!mkdtemp(dir))
        return 1;
    chdir(dir);
    put("secret.txt", "the secret");

    /* Vulnerable: check the name, then use the name. */
    put("report.txt", "a public report");
    if (access("report.txt", R_OK) == 0) {            /* time of check */
        swap_in_symlink("report.txt");                 /* the file changes in the window */
        show("access() then open()", open("report.txt", O_RDONLY));   /* time of use */
    }

    /* Repaired: open first without following links, then check what was opened. */
    unlink("report.txt");
    put("report.txt", "a public report");
    swap_in_symlink("report.txt");
    int fd = open("report.txt", O_RDONLY | O_NOFOLLOW);
    if (fd >= 0) {
        struct stat st;
        if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
            close(fd);
            fd = -1;
        }
    }
    show("open(O_NOFOLLOW) then fstat()", fd);
    unlink("report.txt");
    unlink("secret.txt");
    chdir("/");
    rmdir(dir);
    return 0;
}
```

```text
access() then open(): read "the secret"
open(O_NOFOLLOW) then fstat(): open failed: Too many levels of symbolic links
```

The check passed on a harmless public report, the link was swapped for one that points to `secret.txt`, and `open` followed it: the program read the secret. The repair has two parts. **Do not check names; open once and check what you opened**: `open` with `O_NOFOLLOW` (refuse a symbolic link in the last path component), then `fstat` on the **descriptor** to confirm it is a regular file. The second case printed `open failed: Too many levels of symbolic links` (that is what `O_NOFOLLOW` reports as `ELOOP`).

Other cases of the same pattern: `stat` then `open`, `mkdir` after "does the directory exist", checking permissions of `/tmp/name` and then creating it. The fixes use the atomic forms: `open(..., O_CREAT | O_EXCL)` (fails if it exists), `mkdtemp` and `mkstemp` for temporary files, `openat` and `fstatat` on a directory descriptor you hold (so that renames above it cannot redirect you), and `O_NOFOLLOW`, `O_CLOEXEC`. Problem E asks for a complete, tested function.

---

## 9. CERT C and MISRA C

*This section is from memory and not measured. The CERT C pages at the SEI wiki load, but the fetch tool did not return the rule titles, so the titles below are the ones I remember. Check them against the standard before you quote them.*

The **SEI CERT C Coding Standard** is a numbered rule set with a four-letter category and a number, "-C" at the end. The rules that cover most real vulnerabilities, and where this tutorial showed each:

| Rule | What it says (my wording) | Where |
|---|---|---|
| INT30-C | Unsigned integer operations must not wrap | sections 1 and 7 |
| INT32-C | Signed integer operations must not overflow | section 7 |
| STR31-C | A string destination must have room for the characters and the terminator | section 1, problem B |
| ARR30-C | Do not form or use out-of-bounds pointers or indexes | sections 2 and 3 |
| MEM30-C | Do not access freed memory | section 5 (B3, B4) |
| MEM35-C | Allocate sufficient memory for an object | section 7 |
| EXP34-C | Do not dereference null pointers | section 5 (B2) |
| FIO45-C | Avoid TOCTOU race conditions while accessing files | section 8 (this page loads; the rule requires not using the same name in two file operations) |
| FIO30-C | Exclude user input from format strings | section 6 |
| ERR33-C | Detect and handle standard library errors | the `clang-tidy` `cert-err33-c` check in section 5 |

`clang-tidy`'s `cert-*` checks and `cppcheck`'s add-ons implement parts of this list, which is why the policy diagnostics in section 5 look the way they do.

**MISRA C** (Motor Industry Software Reliability Association) is the coding standard for automotive, and also used in aerospace, medical and industrial code. The documents are sold, so I cannot quote them, and what follows is the outline I know. It defines **directives** and **rules** (MISRA C:2012 and its amendments, and a 2023 edition); each rule is classed mandatory, required or advisory, and a project states a compliance level and records **deviations** with a reason. In outline it asks for: no use of undefined or unspecified behaviour; a restricted set of types and an "essential type" model that forbids implicit conversions that lose value or sign; no dynamic memory (`malloc`, `free`) after start-up (`Dir 4.12` and the standard library rule); no recursion; bounded loops and one exit per function as a style; `switch` always with a `default`; `goto` discouraged; every function's result used or cast to `void`; no function-like macros where a function will do; no dead or unreachable code. The practical effect is "boring C" where static analysis can prove properties. Tools that check it (commercial checkers, and the `misra.py` add-on of `cppcheck`, which needs the rule texts supplied separately) are used alongside review. For an interview: you do not need the numbers; you need to say why embedded code avoids `malloc` (fragmentation and failure at run time, no recovery), recursion (unbounded stack) and implicit conversions, and that a deviation is a documented, reviewed exception.

---

## 10. What the defences cost

The same message parser (200,000 messages of 100 records each) and a hot string copy (`str_copy` into a 64 byte local buffer, 100,000,000 times), timed with the protections off, with the hardened flag set, and under the sanitizers. The checksums printed by the program are equal across builds, so the builds compute the same thing:

```text
T1 parse 200000 messages: checksum off 36402000 3608, hardened 36402000 3608, ASan 36402000 3608, UBSan 36402000 3608; copy into a local buffer 100000000 times: off 18750000000 3608, hardened 18750000000 3608, ASan 18750000000 3608
```

| Build | Parse, ms (run 1 / run 2) | Copy loop, ms (run 1 / run 2) |
|---|---|---|
| all protections off | 81 / 75 | 1029 / 1004 |
| hardened flags | 88 / 76 | 1077 / 1097 |
| UBSan | 161 / 167 | not measured |
| ASan | 636 / 616 | 5165 / 5281 |

- The hardened flags cost **0 to 9 percent**: the parse benchmark is within noise (the second run is 76 against 75), and the copy loop, which has a canary check on every call, is about 5 to 9 percent slower. That is the price of canary, fortify and CET in code that is dominated by short function calls; in code that spends its time elsewhere it is not measurable. Leave them on.
- **UBSan** is about 2 times slower on this workload, **ASan** is 8 times slower on the parser and about 5 times on the copy loop, That is why they are for tests and fuzzing, not for shipping. (A leak or memory error checker such as Valgrind is slower still; I did not measure it here.)

| Tool or defence | Cost | Evidence |
|---|---|---|
| Checked arithmetic (`size_mul`) | One branch per call, no measurable cost in the parser | section 10 |
| Stack protector, fortify, CET, RELRO | 0 to 9 percent | section 10 |
| ASan | 5 to 8 times slower | section 10 |
| UBSan | About 2 times slower | section 10 |
| libFuzzer on the parser | About 570,000 runs per second on one core | section 4 |
| AFL++ with ASan, no persistent mode | About 1,000 runs per second | section 4 |
| Static analysis | Minutes of CI; the best tool found 9 of 12 | section 5 |
| Mutation testing by hand | 24 mutants, one test run each | section 3 |

---

## Common mistakes

1. Believing a length from the input: no check that it fits the destination, or the room left in the input.
2. `pos + k <= n` instead of `n - pos >= k`: the sum can wrap.
3. `malloc(n * size)` with a count that came from outside, or computed in 32 bits: use `reallocarray`, `calloc` or `size_mul`.
4. `strncpy` as a "safe" `strcpy`: no terminator when the source is long, and it zero-fills the whole buffer.
5. `strncat` with the buffer size instead of the room left, one byte too many.
6. `printf(user_text)` and every logging function that takes a format without `__attribute__((format))`.
7. A fuzz harness that discards the result, so the optimiser deletes the call (measured: coverage stays at 1).
8. A fuzz harness that uses a stack or oversized buffer: an over-read of one byte stays inside the buffer and ASan sees nothing. Copy to an exact-size heap block.
9. A fuzz target that calls `exit()`, reads the clock or keeps state between calls.
10. Counting coverage as correctness: a line that ran with no assertion is covered and untested.
11. Trusting the tests that never fail: run them against deliberate defects (mutants) before relying on them.
12. `access()` or `stat()` then `open()`, or "check that the directory exists, then create it": open once and check the descriptor, use `O_EXCL`, `mkstemp`.
13. A signed overflow test `if (a + b < a)`: undefined, the optimiser may remove it.
14. Shipping with the sanitizers on (a 5 to 8 times slowdown and a larger attack surface), or without the platform defences (canary, fortify, PIE, RELRO) because the build system overrides the defaults.
15. Taking "no crash in an hour of fuzzing" as proof: it is a statement about the inputs tried (the missed 1000 record case in section 4).
16. Silencing a static analysis warning without understanding it: it was the only tool that found the NULL after `malloc` or the leaked `FILE`.

---

## Questions out loud

1. What is the invariant that makes a binary parser safe, and why is `n - pos >= k` better than `pos + k <= n`?
2. Why is `strncpy` not a safe `strcpy`? What do `strlcpy` and `snprintf` return, and how do you detect truncation?
3. A message header says "1,000,000 records of 64 bytes". What are the two things the parser must check before it allocates?
4. How do `reallocarray` and `calloc` differ from `malloc(n * size)`? What does each return on overflow?
5. Explain a stack buffer overflow. What does the canary protect, when is it checked, and what can an attacker still do with a canary?
6. `_FORTIFY_SOURCE` stopped an overflow before the canary did. Why, and when does it not work?
7. What are NX, ASLR, PIE and RELRO each protecting, and why is a single information leak enough to defeat ASLR?
8. Why is `printf(buf)` dangerous? What can `%x`, `%s` and `%n` each do?
9. What is a TOCTOU bug? Show the `access` then `open` bug and the fix with `O_NOFOLLOW` and `fstat`.
10. What does a coverage-guided fuzzer do that a random fuzzer does not, and what kind of bug will it still miss?
11. What is the difference between fuzzing with a sanitizer and fuzzing with an oracle (a differential harness)?
12. You have 90 percent line coverage and the tests pass. What does that tell you, and how would you test the tests?
13. Which static analysis tools would you run on a C codebase, and what does each one find that the others miss?
14. Why does embedded and automotive code avoid `malloc` and recursion? What is a MISRA "deviation"?

---

## Problems

Five problems, in order of difficulty. **E (entry level)**, **M (medium)**, **H (hard)**.

**Where to solve them.** Any Linux machine with `gcc`. Problem B needs `-fsanitize=address` (GCC or clang). Problem D needs `clang` with the libFuzzer runtime (on Ubuntu, `sudo apt install clang libclang-rt-18-dev`, the runtime package name follows the clang version). I did not test whether Compiler Explorer can run sanitizer builds, so do these locally. Put `sec.h` (the two regions printed in sections 1 and 2) in a folder called `code` next to the folder where you compile, as the programs expect.

**A (E).** Predict the five lines this program prints, then compile and compare. Pay attention to what each call returns and to the unterminated array `c`.

```c
#include "../code/sec.h"

int main(void)
{
    char a[8];
    char b[8] = "abc";
    char c[4] = {'w', 'x', 'y', 'z'};            /* not terminated */

    size_t r1 = str_copy(a, sizeof a, "hello world");
    printf("1: r=%zu a=\"%s\"\n", r1, a);
    size_t r2 = str_cat(b, sizeof b, "defg");
    printf("2: r=%zu b=\"%s\"\n", r2, b);
    size_t r3 = str_cat(b, sizeof b, "xy");
    printf("3: r=%zu b=\"%s\"\n", r3, b);
    size_t r4 = str_cat(c, sizeof c, "q");
    printf("4: r=%zu c=%.4s\n", r4, c);
    size_t r5 = str_copy(a, 0, "zzz");
    printf("5: r=%zu a=\"%s\"\n", r5, a);
    return 0;
}
```

*How to approach it.* Work in order and track `a` and `b` as strings with a length. `str_copy` returns the length of the **source**; `str_cat` returns the length it tried to make; `size == 0` writes nothing.

**B (E).** Three functions below have a security bug each. Find them, say which tool or flag reports each, run `./pb 1`, `./pb 2` and `./pb 3` under `-fsanitize=address`, then write the repaired versions.

```c
#include "../code/sec.h"

static void show(const char *user)
{
    printf(user);
    printf("\n");
}

static char *join(const char *a, uint16_t alen, const char *b, uint16_t blen)
{
    uint16_t total = alen + blen;
    char *p = malloc(total);
    if (!p)
        return NULL;
    memcpy(p, a, alen);
    memcpy(p + alen, b, blen);
    return p;
}

static void name_of(const char *src)
{
    char name[8];
    strncpy(name, src, sizeof name);
    printf("name: %s\n", name);
}

static char big_a[65535], big_b[8];

int main(int argc, char **argv)
{
    int which = argc > 1 ? atoi(argv[1]) : 0;
    if (which == 1)
        show("%x.%x");
    else if (which == 2)
        free(join(big_a, sizeof big_a, big_b, sizeof big_b));
    else if (which == 3)
        name_of("longer than eight");
    return 0;
}
```

*How to approach it.* For each function ask one question. `show`: who controls the format? `join`: what type is the sum and what is the value of `alen + blen`? `name_of`: after `strncpy`, is `name` a string? Compile with `-Wall -Wextra` and read the warning. Then fix with the helpers of section 1.

**C (M).** Write `join_path(dst, size, dir, name)`, which puts `dir`, a slash and `name` into `dst`. It returns 0 on success and -1 if the result (with its terminator) does not fit; on failure `dst` holds the empty string. Use only `str_copy` and `str_cat`. Write eight checks, including the exact-fit, one-too-long, "the directory alone is too long", "the directory fits and the slash does not", and `size` 0 and 1.

*How to approach it.* Remember what `str_copy` and `str_cat` return and compare with `size`, not `size - 1`. The cases "dir fits but the slash does not" and "size is 1" are where a check against the wrong threshold fails. Write the tests first from the specification, with the `EXPECT` macro of section 3, and get them failing against a deliberately wrong `join_path` (mutate it once) before writing the correct one.

**D (M).** The function `kv_value_len` parses `key=value` from a buffer that is not null terminated. Write a libFuzzer harness, find the bug with the fuzzer, say what it is, and fix it. Then run the fixed target for 20 seconds. Pay attention to the harness: the first version of mine found nothing.

```c
#include <stddef.h>
#include <string.h>

/* Parses "key=value" (not terminated) and returns the length of the value, or -1 if there is no '='. */
static int kv_value_len(const char *s, size_t n)
{
    char val[16];
    const char *eq = memchr(s, '=', n);
    if (!eq)
        return -1;
    size_t len = (size_t)(s + n - (eq + 1));
    if (len > sizeof val)
        return -1;
    memcpy(val, eq + 1, len);
    val[len] = '\0';
    return (int)strlen(val);
}
```

*How to approach it.* `LLVMFuzzerTestOneInput(const uint8_t *, size_t)`, copy the input into a `malloc` block of exactly `size` bytes (so no terminator and no slack), call the function, and **use the result**. Build with `clang -g -O1 -fsanitize=fuzzer,address`. Read the report: which line writes out of bounds, and for which length of value? Then fix the comparison.

**E (H).** Write `char *read_regular(const char *path, size_t max, size_t *len)` that reads a whole file and is safe against TOCTOU and against surprises: it must refuse a symbolic link, a directory and a FIFO (a FIFO must not block), refuse a file larger than `max`, return NULL with `errno` set on every failure, and never leak the descriptor or the buffer. Test it on a regular file, a symbolic link, a directory, a FIFO, a file larger than `max` and a missing file.

*How to approach it.* One `open` with `O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC`; everything after that is on the descriptor (`fstat`, `S_ISREG`, `st_size`); no `stat` or `access` on the path. Use one `goto fail` exit that saves `errno` before `free` and `close` (which may change it). Read in a loop (`read` may return less than asked and may be interrupted: `EINTR`). Compare `st_size` with `max` before you allocate, and allocate `size + 1` for a terminator.

---

## Answers

Machine check of all five answers (each line is a script that ran the programs below and tested their output):

```text
problem A: ok (5 lines of output)
problem B: ok (2 sanitizer reports, fixed twin clean)
problem C: ok (8 checks, 0 failed)
problem D: ok (bug found, fixed target clean)
problem E: ok (6 cases)
```

**A.** The program printed:

```text
1: r=11 a="hello w"
2: r=7 b="abcdefg"
3: r=9 b="abcdefg"
4: r=5 c=wxyz
5: r=3 a="hello w"
```

1. `str_copy` into 8 bytes returns the length of `"hello world"` (11), not what was stored: the buffer holds 7 characters and the terminator (`hello w`). Truncation is `r >= size`, here 11 >= 8.
2. `b` is `abc` (length 3). `str_cat` wants to make `abcdefg` (7 characters), which fits in 8 with the terminator: returns 7.
3. Now `b` has 7 characters and the room is 8. Adding `xy` would make 9: nothing past the 7th character is stored, the result is truncated (the buffer still holds `abcdefg`) and the return value is 9, which is `>= 8`: truncation reported.
4. `c` is not terminated inside its 4 bytes. `strnlen(dst, 4)` returns 4, equal to `size`, so `str_cat` does nothing and returns `size + strlen(src)` = 5. A plain `strcat` would read past the array looking for the terminator.
5. With `size == 0` nothing is written (`a` is unchanged) and the return value is still the length of the source (3).

**B.** The three bugs and the tool that reports each:

- `show`: `printf(user)`. The format is under the caller's control: `%x` leaks, `%n` writes. GCC reports it at compile time:

```text
warning: format not a string literal and no format arguments [-Wformat-security]
```

- `join`: `uint16_t total = alen + blen`. With `alen` 65,535 and `blen` 8 the sum is 65,543 and `total` is 7. `malloc(7)`, then a copy of 65,535 bytes. AddressSanitizer reports the write.
- `name_of`: `strncpy(name, src, sizeof name)` copies 8 characters and does not terminate, and `printf("%s")` reads on. ASan reports the read.

```text
$ ./pba 1
$ ./pba 2
ERROR: AddressSanitizer: heap-buffer-overflow on address ADDR at pc ADDR bp ADDR sp ADDR
WRITE of size 65535 at ADDR thread T0
    #1 join pb.c:15
    #2 main pb.c:35
    #3 __libc_start_call_main libc_start_call_main.h:58
ADDR is located 0 bytes after 7-byte region [ADDR,ADDR)
    #1 join pb.c:12
    #2 main pb.c:35
$ ./pba 3
ERROR: AddressSanitizer: stack-buffer-overflow on address ADDR at pc ADDR bp ADDR sp ADDR
READ of size 9 at ADDR thread T0
    #3 name_of pb.c:24
Address ADDR is located in stack of thread T0 at offset 40 in frame
    #0 name_of pb.c:21
SUMMARY: AddressSanitizer: stack-buffer-overflow ../../../../src/libsanitizer/sanitizer_common/sanitizer_common_interceptors_format.inc:563 in printf_common
```

(The first command, `./pba 1`, printed nothing the sanitizer cares about: the format string bug leaks values but is not a memory error, which is why the static warning above matters and why section 5 lists B8 as missed by ASan.) The repaired program:

```c
#include "../code/sec.h"

static void show(const char *user)
{
    printf("%s", user);
    printf("\n");
}

static char *join(const char *a, size_t alen, const char *b, size_t blen)
{
    size_t total;
    if (size_add(alen, blen, &total))
        return NULL;
    char *p = malloc(total ? total : 1);
    if (!p)
        return NULL;
    memcpy(p, a, alen);
    memcpy(p + alen, b, blen);
    return p;
}

static void name_of(const char *src)
{
    char name[8];
    if (str_copy(name, sizeof name, src) >= sizeof name)
        printf("name truncated: ");
    printf("name: %s\n", name);
}

static char big_a[65535], big_b[8];

int main(void)
{
    show("%x.%x");
    char *p = join(big_a, sizeof big_a, big_b, sizeof big_b);
    printf("join: %s\n", p ? "ok" : "failed");
    free(p);
    name_of("longer than eight");
    return 0;
}
```

```text
%x.%x
join: ok
name truncated: name: longer
```

The format is a constant, the sum is in `size_t` and goes through `size_add`, and truncation is detected from the return value of `str_copy`. `join` copes with a total up to `SIZE_MAX`, and fails with NULL when the sum would wrap.

**C.** One solution with the eight checks:

```c
#include <errno.h>
#include "../code/sec.h"

/* dir + "/" + name into dst; 0 on success, -1 (and dst = "") if it does not fit. */
static int join_path(char *dst, size_t size, const char *dir, const char *name)
{
    if (size == 0)
        return -1;
    dst[0] = '\0';
    if (str_copy(dst, size, dir) >= size || str_cat(dst, size, "/") >= size ||
        str_cat(dst, size, name) >= size) {
        dst[0] = '\0';
        return -1;
    }
    return 0;
}

static int fails;
#define EXPECT(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

int main(void)
{
    char buf[16];
    EXPECT(join_path(buf, sizeof buf, "usr", "bin") == 0 && strcmp(buf, "usr/bin") == 0);
    EXPECT(join_path(buf, sizeof buf, "", "x") == 0 && strcmp(buf, "/x") == 0);
    EXPECT(join_path(buf, sizeof buf, "abcdefg", "hijklmn") == 0 && strcmp(buf, "abcdefg/hijklmn") == 0);   /* 15 chars: exact fit */
    EXPECT(join_path(buf, sizeof buf, "abcdefg", "hijklmno") == -1 && buf[0] == '\0');                     /* 16 chars: one too many */
    EXPECT(join_path(buf, sizeof buf, "0123456789abcdef", "x") == -1 && buf[0] == '\0');                    /* dir alone too long */
    EXPECT(join_path(buf, sizeof buf, "0123456789abcde", "x") == -1 && buf[0] == '\0');                     /* dir fits, slash does not */
    EXPECT(join_path(buf, 0, "a", "b") == -1);
    EXPECT(join_path(buf, 1, "", "") == -1 && buf[0] == '\0');
    printf("%d failed\n", fails);
    return fails != 0;
}
```

```text
0 failed
```

The function resets `dst` to the empty string before it starts and on every failure, so a caller that ignores the return value still has a valid (empty) string. Each step compares the return value with `size`, not `size - 1`: a result of exactly `size` means the terminator did not fit. The "dir fits, slash does not" case is `"0123456789abcde"` (15 characters) in a 16 byte buffer: `str_copy` returns 15, which is less than 16, and then `str_cat(dst, size, "/")` returns 16, which is not.

**D.** The harness, with the result kept in a `volatile`:

```c
#include <stdint.h>
#include <stdlib.h>
#include "kv.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char *copy = malloc(size ? size : 1);      /* exact size, no terminator: over-reads are visible */
    if (!copy)
        return 0;
    memcpy(copy, data, size);
    volatile int sink = kv_value_len(copy, size);   /* a used result keeps the optimiser from deleting the call */
    (void)sink;
    free(copy);
    return 0;
}
```

Without it, the optimiser deleted the call (section 4, 26 million runs, coverage 1):

```text
$ ./pd_old -seed=1 -max_len=64 -max_total_time=20 pdo   (result of kv_value_len unused)
INFO: Loaded 1 PC tables (1 PCs): 1
#2 INITED cov: 1 ft: 1 corp: 1/1b
Done 26446092 runs in 21 second(s)
```

With it, the fuzzer finds the bug after a few hundred executions:

```text
$ ./pd_fuzz -seed=1 -max_len=64 pdc   (result kept in a volatile)
INFO: Loaded 1 PC tables (7 PCs): 7
#2 INITED cov: 3 ft: 3 corp: 1/1b
#596 NEW cov: 4 ft: 4 corp: 2/2b lim: 8
ERROR: AddressSanitizer: stack-buffer-overflow on address ADDR at pc ADDR bp ADDR sp ADDR
WRITE of size 1 at ADDR thread T0
 #0 kv_value_len kv.h:15:14
 #1 LLVMFuzzerTestOneInput pd_fuzz.c:11:25
SUMMARY: AddressSanitizer: stack-buffer-overflow kv.h:15:14 in kv_value_len
```

A `stack-buffer-overflow`, a write of 1 byte at `kv.h:15`: `val[len] = '\0'` when `len == 16`. The check `len > sizeof val` lets 16 through, `memcpy` fills the array, and the terminator goes one past it. The fix is `len >= sizeof val`: the value plus its terminator must fit. The fixed target, 20 seconds:

```text
$ ./pd_fixed -seed=1 -max_len=64 -max_total_time=20 pdc2   (kv.h with len >= sizeof val)
INFO: Loaded 1 PC tables (7 PCs): 7
#2 INITED cov: 3 ft: 3 corp: 1/1b
#596 NEW cov: 4 ft: 4 corp: 2/2b lim: 8
#1557 NEW cov: 5 ft: 5 corp: 3/19b lim: 17
Done 17846587 runs in 21 second(s)
```

About 17.8 million runs, no report. (I also checked by hand that the input `k=0123456789abcdef` crashes the buggy build under ASan at `-O0`, `-O1` and `-O2`.) A boundary at exactly the size of the buffer is where fuzzers and tests find most bugs: write a test for `len == size - 1`, `len == size` and `len == size + 1`.

**E.** The solution with its test program:

```c
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../code/sec.h"

/* Reads a regular file of at most max bytes. Returns a malloc'd buffer (length in *len) or NULL with errno set.
   No path check happens before the open: every check is on the descriptor that was opened. */
static char *read_regular(const char *path, size_t max, size_t *len)
{
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return NULL;
    struct stat st;
    char *buf = NULL;
    if (fstat(fd, &st) != 0)
        goto fail;
    if (!S_ISREG(st.st_mode)) {
        errno = EINVAL;
        goto fail;
    }
    if (st.st_size < 0 || (uintmax_t)st.st_size > max) {
        errno = EFBIG;
        goto fail;
    }
    buf = malloc((size_t)st.st_size + 1);
    if (!buf)
        goto fail;
    size_t got = 0;
    while (got < (size_t)st.st_size) {
        ssize_t r = read(fd, buf + got, (size_t)st.st_size - got);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            break;
        got += (size_t)r;
    }
    buf[got] = '\0';
    close(fd);
    *len = got;
    return buf;
fail:;
    int e = errno;
    free(buf);
    close(fd);
    errno = e;
    return NULL;
}

static void try(const char *what, const char *path, size_t max)
{
    size_t len = 0;
    char *p = read_regular(path, max, &len);
    if (p)
        printf("%-22s ok, %zu bytes: \"%s\"\n", what, len, p);
    else
        printf("%-22s failed: %s\n", what, strerror(errno));
    free(p);
}

int main(void)
{
    char dir[] = "/tmp/pe_XXXXXX";
    if (!mkdtemp(dir))
        return 1;
    char f[64], l[64], d[64], q[64], big[64];
    snprintf(f, sizeof f, "%s/file", dir);
    snprintf(l, sizeof l, "%s/link", dir);
    snprintf(d, sizeof d, "%s/sub", dir);
    snprintf(q, sizeof q, "%s/fifo", dir);
    snprintf(big, sizeof big, "%s/big", dir);
    FILE *fp = fopen(f, "w");
    fputs("the secret", fp);
    fclose(fp);
    fp = fopen(big, "w");
    for (int i = 0; i < 100; ++i)
        fputc('x', fp);
    fclose(fp);
    if (symlink(f, l) || mkdir(d, 0700) || mkfifo(q, 0600))
        return 1;
    try("regular file", f, 1000);
    try("symlink to the file", l, 1000);
    try("directory", d, 1000);
    try("fifo", q, 1000);
    try("100 bytes, max 50", big, 50);
    try("missing file", "/tmp/pe_does_not_exist", 1000);
    unlink(f); unlink(l); unlink(q); unlink(big); rmdir(d); rmdir(dir);
    return 0;
}
```

```text
regular file           ok, 10 bytes: "the secret"
symlink to the file    failed: Too many levels of symbolic links
directory              failed: Invalid argument
fifo                   failed: Invalid argument
100 bytes, max 50      failed: File too large
missing file           failed: No such file or directory
```

Why it is safe: there is one `open`, which refuses a symbolic link (`ELOOP`, "Too many levels of symbolic links") and does not block on a FIFO (`O_NONBLOCK`); every check after that is on the descriptor, so nothing can change the object between the check and the use; the directory and the FIFO are rejected by `S_ISREG` (reported as `EINVAL`); the size limit is tested before the allocation, with `uintmax_t` so that a negative or huge `st_size` cannot wrap; the loop handles short reads and `EINTR`; the `fail` label saves `errno` across `free` and `close`. A file that is truncated or grows between `fstat` and `read` is still handled: the loop reads at most `st_size` bytes and stops at end of file, and the length returned is what was read.

---

## Cheat sheet

- Every length from outside is hostile. Track the room left (`n - pos`) and compare it with the length; never add before comparing. Check the count before allocating from it.
- Size arithmetic: `size_mul`, `size_add` (builtins), `reallocarray`, `calloc`. Compute in `size_t`. Never rely on a signed overflow test.
- Strings: `strlcpy` and `strlcat` semantics (return the length you wanted, compare with `size`). `strncpy` does not terminate; `strncat` counts the room for the appended part.
- Test with an independent oracle, exact-size heap copies and the sanitizers. Test the tests: mutants (23 of 24 caught here, and the escape was explained). Coverage is what ran, not what was checked.
- Fuzz: libFuzzer for functions (`-fsanitize=fuzzer,address`), AFL++ for programs; a differential harness gives the fuzzer an oracle. Keep the result used, no `exit`, no state, keep the corpus as regression tests. A fuzzer misses bugs that need a large exact input.
- Static analysis: `-fanalyzer`, `clang-tidy`, `cppcheck` each found 9 of 12 and not the same 9; GCC's warnings found 7; the run-time sanitizers 8. Run all of them. The sanitizers depend on the optimisation level.
- Defences: canary (detects after the overflow), `_FORTIFY_SOURCE` (stops at the call, needs a known size), NX, ASLR and PIE (need an address leak to defeat), RELRO, CET. Ubuntu GCC enables all of them; cost 0 to 9 percent. ASan 5 to 8 times, UBSan 2 times: for tests only.
- Format strings: a constant format, always; `-Wformat-security`; `__attribute__((format))` on your own loggers.
- TOCTOU: do not check a name and then use the name. `open` once with `O_NOFOLLOW`, `fstat` the descriptor, `O_EXCL` and `mkstemp` to create, `openat` relative to a held directory.
- CERT C: INT30-C, STR31-C, ARR30-C, MEM30-C, MEM35-C, EXP34-C, FIO30-C, FIO45-C, ERR33-C (from memory). MISRA C: restrict the language, no dynamic memory or recursion, documented deviations (from memory).

---

## Sources

- libFuzzer: `LLVMFuzzerTestOneInput`, `-fsanitize=fuzzer`, `-max_len`, `-max_total_time`, corpus and `crash-` artifacts, checked ([LLVM libFuzzer](https://llvm.org/docs/LibFuzzer.html)).
- AFL++: `afl-cc`, `afl-fuzz -i -o`, seeds and `AFL_USE_ASAN`, checked ([AFL++ fuzzing in depth](https://aflplus.plus/docs/fuzzing_in_depth/)).
- `-fanalyzer` and its warnings (`-Wanalyzer-malloc-leak`, `-Wanalyzer-double-free`, `-Wanalyzer-use-after-free`), checked ([GCC static analyzer options](https://gcc.gnu.org/onlinedocs/gcc/Static-Analyzer-Options.html)).
- `-fstack-protector-strong`, `-fsanitize=address`, `-fsanitize=undefined` and `--coverage`, checked ([GCC instrumentation options](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html)).
- `access(2)` and the race between the check and the use, checked ([access(2)](https://man7.org/linux/man-pages/man2/access.2.html)).
- `reallocarray`: fails safely on overflow, added in glibc 2.26, checked ([reallocarray(3)](https://man7.org/linux/man-pages/man3/reallocarray.3.html)).
- CERT C rule pages (the pages load; the titles in section 9 are from memory): [INT30-C](https://wiki.sei.cmu.edu/confluence/display/c/INT30-C.+Ensure+that+unsigned+integer+operations+do+not+wrap), [STR31-C](https://wiki.sei.cmu.edu/confluence/display/c/STR31-C.+Guarantee+that+storage+for+strings+has+sufficient+space+for+character+data+and+the+null+terminator), [FIO45-C](https://wiki.sei.cmu.edu/confluence/display/c/FIO45-C.+Avoid+TOCTOU+race+conditions+while+accessing+files).
- MISRA C: not checked (the documents are paid); section 9 is from memory.
- Not checked: the `clang-tidy` and `cppcheck` manuals (their behaviour here is measured, not read), Unity and CMocka (not installed), Valgrind (not run).
- C 09 for the failing allocator, C 10 for `goto` cleanup, `__attribute__((format))` and sanitizers. C 12 (the C interview question bank) continues from here.
