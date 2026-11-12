# C 07: Strings, the Standard Library and Bit Manipulation

*Part of the [C language plan](00_plan.md) · Time: about 4 hours (2.5 reading, 1.5 problems) · You need: [C 02](02_pointers_arrays_and_functions.md) for arrays and pointers, [C 04](04_structs_unions_bitfields_and_alignment.md) for integer layout, and [C 06](06_preprocessor_compilation_and_linking.md) for what runs at exit. There is no companion demo file: every program below was compiled and run, and its real output is printed under it*

Most C bugs that reach production are not exotic. They are a string copy that did not terminate, a `memcpy` on overlapping buffers, an `atoi` that turned garbage into zero, a `printf` whose output was copied by a `fork`, a shift by the wrong amount, a mask with the wrong precedence. This tutorial takes the part of the standard library you use every day and shows exactly where each function lets you down, what the safer replacement is, and how to write the replacement yourself, because "write `memmove`", "write `atoi`" and "count the set bits" are the classic interview questions for systems roles. The second half is bit manipulation: the idioms, the register-field style used in drivers and firmware, and the undefined behaviour that lives in shifts and integer promotion.

By the end you can:

- say why `strncpy` and `strncat` are not "safe `strcpy` and `strcat`", predict what they leave in the buffer, and use `snprintf` or a `strlcpy`-style copy that reports truncation;
- write `strlen`, `strcmp`, `memcpy`, `memmove`, `strlcpy`, `itoa` and a strict integer parser, and explain the overlap rule that separates `memcpy` from `memmove`;
- choose `strtol` over `atoi` and use `endptr`, `errno` and the range checks correctly;
- explain why `strtok` is not thread-safe, why it breaks nested loops and why it crashes on a literal, and use `strtok_r`;
- write `qsort` comparators that cannot overflow, and call `bsearch`;
- describe the three stdio buffering modes, predict how many `write` calls a program makes, and explain why `printf` before `fork` can print twice;
- set, clear, toggle and test bits; count, reverse and rotate them; round up to a power of two; extract and insert register fields; sign-extend a narrow field;
- list the shift and promotion rules that cause undefined or surprising results, and explain endianness on the wire.

**Where the numbers come from.** Every program and command was run on an x86-64 Linux container (Ubuntu, glibc 2.39, GCC 13.3). Code is compiled with `-std=gnu11 -Wall -Wextra -Wno-unused-function`; the header that holds the helpers defines many `static` functions and each demo uses only some of them, so that one warning is turned off. The helper code printed in this tutorial (the memory and string functions, the integer parser and `itoa`, the reentrant tokenizer and the bit toolkit) is the real source and was tested against oracles: 857,172 checks passed under AddressSanitizer and UBSan, comparing each function with the C library on random inputs (`memmove`, `strcmp`, `snprintf`, `strtol`, `strtok_r`, `__builtin_popcount`, `__builtin_bswap32`) and with slow reference loops. I then tried 19 deliberate bugs in it; 18 were caught (16 by a failed check, 2 by the sanitizers). The 19th is a defensive line in `parse_int` that turned out to be redundant: removing it changes nothing, because the later check on trailing characters already rejects the same inputs. I kept the line and did not count it as a miss. The tool outputs are real; compiler warnings are shown where they teach something. Statements about ARM64, Clang, musl, Windows and exact glibc internals are from memory and marked.

**Data and timing.** Machine: a cloud container, Intel Xeon 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Timings are the best of 3 wall-clock runs in milliseconds, shown as run 1 / run 2 (two complete repetitions of the whole benchmark). They show the shape of the cost on this machine and will differ on yours.

---

## 1. What a C string is

A C string is an array of `char` that ends at the first `'\0'` byte. That is the whole definition, and every property below follows from it:

- The length is not stored; `strlen` walks the array, so it is O(n), and calling it in a loop condition makes the loop O(n²).
- The terminator needs a byte of space: `char name[8]` holds at most 7 characters.
- A string literal such as `"abc"` is an array of 4 bytes in read-only memory (C 03). Writing to it is undefined behaviour.
- `sizeof` gives the size of the array (the capacity), `strlen` gives the position of the first NUL (the length). On a pointer, `sizeof` is the size of the pointer.
- Library functions trust you. None of them can check that a destination is large enough or that a source is terminated; they read and write until they reach the byte they were told to stop at.
- `char` may be signed or unsigned (it is signed on x86-64 Linux and unsigned on ARM64 Linux, from memory). The `<ctype.h>` functions need a value in the range of `unsigned char`, so cast first: `isalpha((unsigned char)c)`.

The functions in the table are the ones to know, with the habit that goes with each:

| Function | What it does | The catch | Use instead |
|---|---|---|---|
| `gets` | read a line | cannot be used safely; removed from C11 | `fgets` |
| `strcpy`, `strcat`, `sprintf` | copy, append, format | no bound at all | `snprintf`, or a copy that reports truncation |
| `strncpy` | copy at most n bytes | may not terminate; pads with zeros to n | `snprintf`, `strlcpy`-style copy |
| `strncat` | append at most n bytes | n is the room for new characters, not the buffer size | `snprintf` |
| `strlen`, `strcmp` | length, ordering | need a terminator | `strnlen` when the source may be unterminated |
| `memcpy` | copy bytes | regions must not overlap | `memmove` |
| `atoi`, `atol` | text to number | no error reporting | `strtol` |
| `strtok` | split a string | hidden static state; modifies the input | `strtok_r`, `strsep` |
| `ctime`, `asctime`, `localtime` | time to text | return a static buffer | `ctime_r`, `localtime_r`, `strftime` |
| `scanf("%s")` | read a word | no bound | a width (`%31s`), or `fgets` and `sscanf` |

## 2. The `strncpy` and `strncat` traps, and the copy you want

`strncpy` was designed for fixed-width, NUL-padded fields in old record formats, not as a bounded `strcpy`. It always writes exactly `n` bytes: if the source is shorter, the rest is filled with zeros; if the source is `n` bytes or longer, it copies `n` bytes and writes **no terminator**.

```c
#include <stdio.h>
#include <string.h>

static void hexdump(const char *label, const char *b, int n)
{
    printf("%-26s", label);
    for (int i = 0; i < n; ++i)
        printf(" %02x", (unsigned char)b[i]);
    printf("\n");
}

int main(void)
{
    char buf[8];
    memset(buf, 'X', sizeof buf);
    strncpy(buf, "abcdefghij", sizeof buf);
    hexdump("strncpy, source too long:", buf, 8);
    printf("%-26s %s\n", "terminated within 8 bytes?", memchr(buf, '\0', 8) ? "yes" : "NO");

    memset(buf, 'X', sizeof buf);
    strncpy(buf, "hi", sizeof buf);
    hexdump("strncpy, source short:", buf, 8);

    char c[16] = "ab";
    strncat(c, "cdefgh", 3);
    printf("%-26s %s\n", "strncat(c, \"cdefgh\", 3):", c);

    char s[8];
    int r = snprintf(s, sizeof s, "%s", "abcdefghij");
    printf("%-26s s=\"%s\" returned %d (needed %d plus the terminator)\n", "snprintf, too long:", s, r, r);
    r = snprintf(s, sizeof s, "%d", 42);
    printf("%-26s s=\"%s\" returned %d\n", "snprintf, fits:", s, r);
    return 0;
}
```

```text
strncpy, source too long:  61 62 63 64 65 66 67 68
terminated within 8 bytes? NO
strncpy, source short:     68 69 00 00 00 00 00 00
strncat(c, "cdefgh", 3):   abcde
snprintf, too long:        s="abcdefg" returned 10 (needed 10 plus the terminator)
snprintf, fits:            s="42" returned 2
```

- With a 10 character source and an 8 byte buffer, `strncpy` filled all 8 bytes with `a` to `h` and the buffer has no terminator within its 8 bytes. Any later `printf("%s", buf)` reads past the end. The compiler saw the problem and said so:

```text
p1_strn.c:16:5: warning: 'strncpy' output truncated copying 8 bytes from a string of length 10 [-Wstringop-truncation]
p1_strn.c:29:36: warning: '%s' directive output truncated writing 10 bytes into a region of size 8 [-Wformat-truncation=]
```

- With a short source the call wrote `hi` and then six zero bytes: the padding is a cost as well as a fact. It is measured in section 2.1.
- `strncat(c, "cdefgh", 3)` appended 3 characters and **always** terminates, but its third argument is the number of characters to append, not the size of the buffer. Code that passes `sizeof buf` to it overflows exactly where the programmer thought it was protected.
- `snprintf(s, size, fmt, ...)` writes at most `size - 1` characters, always terminates when `size > 0`, and **returns the length it would have written** if there had been room. A return value of `size` or more means truncation. The result is the check a bounded copy should give you, in one call.

The copy you usually want is the one OpenBSD introduced as `strlcpy`: copy at most `size - 1` bytes, always terminate, and return the length of the source so the caller can detect truncation (`if (n >= size)`). glibc added `strlcpy` and `strlcat` in version 2.38; the program below calls the library version on this machine's glibc 2.39:

```text
libc strlcpy: d="abcdefg" returned 10
```

On older systems or other libc implementations, write your own. This one is tested against `snprintf` on random source lengths 0 to 19 and buffer sizes 0 to 24, including "does not touch the buffer when size is 0":

```c
/* Length of s. */
static size_t xstrlen(const char *s)
{
    const char *p = s;
    while (*p)
        ++p;
    return (size_t)(p - s);
}

/* memcpy: copies forward; the regions must not overlap. */
static void *xmemcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

/* memmove: correct when the regions overlap. */
static void *xmemmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d == s || n == 0)
        return dst;
    if ((uintptr_t)d < (uintptr_t)s) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

/* Compare as unsigned char, like the standard strcmp. */
static int xstrcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* strlcpy: copies at most size-1 bytes, always terminates when size > 0,
   returns the length of src so the caller can detect truncation. */
static size_t xstrlcpy(char *dst, const char *src, size_t size)
{
    size_t n = xstrlen(src);
    if (size) {
        size_t c = n < size - 1 ? n : size - 1;
        xmemcpy(dst, src, c);
        dst[c] = '\0';
    }
    return n;
}
```

(The same listing holds `xmemcpy`, `xmemmove` and `xstrcmp`, which the next sections explain.) Three details to notice in `xstrcmp`: the comparison is done on `unsigned char` (the standard requires it, and a signed comparison orders bytes 128 to 255 before 0 to 127, a bug the test suite caught in one of my deliberate mutants), the result is the difference of the first differing bytes, and only the sign of the result is defined, so never compare it to 1 or -1.

### 2.1 What the padding costs

`strncpy` writing 4096 bytes for a two character string is wasted work. 2,000,000 copies of `"hi"` into a 4096 byte buffer:

```text
T2 2000000 copies of a 2 character string into a 4096 byte buffer: checksums strncpy 210000000, strlcpy 210000000, snprintf with the format <%s> 418000000 (it writes <hi>, so its sum differs)
```

| 2,000,000 copies of `"hi"`, 4096 byte buffer | Time ms (run 1 / run 2) |
|---|---|
| `strncpy` (pads 4094 zero bytes each time) | 63.00 / 54.00 |
| `xstrlcpy` (copies 2 bytes and a terminator) | 21.00 / 20.00 |
| `snprintf(dst, size, "<%s>", s)` | 88.00 / 88.00 |

`strncpy` took about 2.5 to 3 times as long as the `strlcpy`-style copy, and `snprintf` was the slowest of the three because it parses the format string every time. The point is not that `snprintf` is slow (88 ms for 2 million calls is 44 ns each) but that the padding is real work: with a 64 KB buffer instead of 4 KB there would be sixteen times as many padding bytes. The three checksums agree for `strncpy` and `xstrlcpy`; the `snprintf` sum differs only because its format adds `<` and `>`.

## 3. `memcpy`, `memmove`, `memset`, `memcmp`

`memcpy(dst, src, n)` copies `n` bytes and is allowed to assume that the two regions do **not** overlap; if they do, the behaviour is undefined. `memmove` is defined for any overlap. The difference is the direction of the copy:

- If the destination starts below the source, copying forward is safe (each byte is read before it is overwritten).
- If the destination starts above the source and the regions overlap, a forward copy overwrites source bytes before they are read; copy backward instead.

A forward byte loop shows what an overlapping `memcpy` would be allowed to do. (This is my own function, whose behaviour is defined; I did not call the libc `memcpy` on overlapping buffers, because the result is undefined and would prove nothing.)

```c
#include <stdio.h>
#include <string.h>
#include "../code/str.h"

int main(void)
{
    char a[16], b[16], c[16];
    strcpy(a, "abcdefgh");
    strcpy(b, "abcdefgh");
    strcpy(c, "abcdefgh");
    xmemcpy(a + 2, a, 6);
    xmemmove(b + 2, b, 6);
    memmove(c + 2, c, 6);
    printf("shift right by 2 (dst above src, overlapping)\n");
    printf("  forward copy (xmemcpy): %s\n", a);
    printf("  xmemmove:               %s\n", b);
    printf("  libc memmove:           %s\n", c);

    strcpy(a, "abcdefgh");
    strcpy(b, "abcdefgh");
    xmemcpy(a, a + 2, 6);
    xmemmove(b, b + 2, 6);
    printf("shift left by 2 (dst below src, overlapping)\n");
    printf("  forward copy (xmemcpy): %s\n", a);
    printf("  xmemmove:               %s\n", b);
    return 0;
}
```

```text
shift right by 2 (dst above src, overlapping)
  forward copy (xmemcpy): abababab
  xmemmove:               ababcdef
  libc memmove:           ababcdef
shift left by 2 (dst below src, overlapping)
  forward copy (xmemcpy): cdefghgh
  xmemmove:               cdefghgh
```

Shifting `abcdefgh` right by two should give `ababcdef`. The forward copy produced `abababab`, because it copied `a` into position 2 and then read position 2 as the source for position 4. `xmemmove` and the library `memmove` gave `ababcdef`. When the destination is below the source (shift left) both directions give `cdefghgh`, which is why a `memcpy` bug that needs overlap in the other direction can sit unnoticed for years. `xmemmove` compares the addresses through `uintptr_t`: comparing pointers into different arrays with `<` is not defined by the standard, and the library does it with knowledge of the platform. The listing in section 2 has the code.

The library `memcpy` is much faster than a byte loop, and it is not the loop you wrote with a different name. 16 MiB copied 60 times:

```text
T1 16 MiB copied 60 times: checksum libc 7489, loop 7489, loop without pattern recognition 7489; memcpy calls inside the compiled loop function: 0 in the default build, 0 with -fno-tree-loop-distribute-patterns
```

| 16 MiB copied 60 times | Time ms (run 1 / run 2) |
|---|---|
| library `memcpy` | 92.00 / 97.00 |
| `xmemcpy` byte loop, `-O2` | 375.00 / 380.00 |
| `xmemcpy` with `-fno-tree-loop-distribute-patterns` | 383.00 / 375.00 |

The byte loop took about four times as long, and GCC did not turn it into a call to `memcpy` (no call appears in the compiled function, with or without the option that disables that recognition, and the times are the same). My explanation, which I did not test, is that the loop is defined for overlapping regions (it copies forward) and `memcpy` is not, so the compiler may not replace one by the other without knowing the pointers do not overlap. The practical rule is to call the library: it uses wide and vector loads and stores, and chooses the code by CPU features at run time (from memory).

Two more points about this family:

- **`memcmp` is not for structs.** Padding bytes (C 04) have unspecified values, so two equal structs can differ in `memcmp`; compare field by field. `memcmp` also returns the difference of the first differing bytes, not an ordering for integers (it orders by byte, so little-endian integers do not sort by it).
- **`memset` to clear a secret can be removed.** After the last use of a local buffer the compiler may delete the `memset` as a dead store. `explicit_bzero` (glibc, BSDs) or `memset_s` (optional C11 Annex K) are designed not to be deleted. This is the same code compiled with `-O2`, and the functions each call:

```text
$ gcc -O2 -c p10_dse.c   and the functions each one calls (objdump -dr)
with_memset:          __snprintf_chk use __stack_chk_fail
with_explicit_bzero:  __snprintf_chk use __explicit_bzero_chk __stack_chk_fail
```

`with_memset` calls `__snprintf_chk`, `use` and the stack-protector check, and no `memset`: the compiler removed it. `with_explicit_bzero` still calls `__explicit_bzero_chk` (the checked form that Ubuntu's `_FORTIFY_SOURCE` substitutes). The `_chk` names are Ubuntu's default hardening, not part of the language.

## 4. Numbers in text: `atoi`, `strtol`, `itoa`

`atoi("abc")` returns 0, and so does `atoi("0")`: there is no way to tell failure from a valid zero, and a value that does not fit in an `int` is undefined behaviour. `strtol` reports everything: where parsing stopped (`endptr`), that the value was out of range (`errno == ERANGE`, with `LONG_MIN` or `LONG_MAX` returned), and whether any digits were found (`endptr == s`). The rules:

- Leading white space is skipped; one optional `+` or `-` is accepted.
- Base 0 means "decide from the prefix": `0x` is hex, `0` is octal, otherwise decimal. In base 16 an optional `0x` is accepted.
- It stops at the first character that is not part of the number; whether that is an error is **your** decision, which is why the code checks that `*endptr` is the terminator.
- `errno` is set only on failure; you must set it to 0 before the call, otherwise an old value looks like an error.
- `strtoul` accepts a minus sign and wraps: `strtoul("-1")` is `ULONG_MAX` (below). Use `strtol` and check the sign if negative input must be rejected.

```c
typedef enum { PI_OK, PI_EMPTY, PI_SYNTAX, PI_RANGE } ParseStatus;

/* Parse a whole string as an int. Leading white space and a sign are
   accepted (strtol rules); trailing characters are not. */
static ParseStatus parse_int(const char *s, int base, int *out)
{
    char *end;
    long v;
    if (!s || !*s)
        return PI_EMPTY;
    errno = 0;
    v = strtol(s, &end, base);
    if (end == s)
        return PI_SYNTAX;
    if (errno == ERANGE || v < INT_MIN || v > INT_MAX)
        return PI_RANGE;
    if (*end != '\0')
        return PI_SYNTAX;
    *out = (int)v;
    return PI_OK;
}

#define ITOA_BUF 34 /* 32 binary digits, a sign and the terminator */

/* Integer to text in base 2..36; a minus sign for negative values. */
static char *xitoa(int value, char *buf, int base)
{
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char tmp[ITOA_BUF];
    int n = 0, neg = value < 0;
    unsigned u = neg ? 0u - (unsigned)value : (unsigned)value;
    char *p = buf;
    do {
        tmp[n++] = digits[u % (unsigned)base];
        u /= (unsigned)base;
    } while (u);
    if (neg)
        *p++ = '-';
    while (n)
        *p++ = tmp[--n];
    *p = '\0';
    return buf;
}

/* Reentrant tokenizer: all state is in *save (the same contract as strtok_r). */
static char *xstrtok_r(char *s, const char *delims, char **save)
{
    char *e;
    if (!s)
        s = *save;
    s += strspn(s, delims);
    if (*s == '\0') {
        *save = s;
        return NULL;
    }
    e = s + strcspn(s, delims);
    if (*e)
        *e++ = '\0';
    *save = e;
    return s;
}
```

(That listing also holds `xitoa` and `xstrtok_r`.) `parse_int` rejects an empty string, text with no digits, trailing characters, and values outside `int`, and leaves `*out` untouched on every failure (a checked property). Compare it with `atoi`:

```text
input          atoi         parse_int (base 10)
"42"           42           ok, 42
"abc"          0            syntax
""             0            empty
"12abc"        12           syntax
"7 "           7            syntax
" 7"           7            ok, 7
"-2147483648"  -2147483648  ok, -2147483648
"2147483648"   -2147483648  range
"0x1F"         0            syntax
"0x1F" in base 16: ok, 31
"0x1F" in base 0:  ok, 31
```

`atoi` returns 12 for `"12abc"` and 7 for `"7 "` with no sign that anything was wrong, and `-2147483648` for `"2147483648"` (glibc truncates a `long`; the standard says undefined, so another library may do something else). `parse_int` gives a status for each. The `strtoul` surprise:

```text
strtoul("-1") = 18446744073709551615
```

`xitoa` is the other classic: convert an `int` to text in any base from 2 to 36. The two points that fail interviews: build the digits backwards and reverse them, and handle the most negative number, because `-value` overflows for `INT_MIN`. Negate in unsigned arithmetic (`0u - (unsigned)value`), which is defined and gives 2147483648. The buffer must hold 32 binary digits, a sign and the terminator: `ITOA_BUF` is 34. The tests round-trip every base through `strtol` and compare base 10, 8 and 16 with `snprintf`; one of the deliberate bugs, `-value` on a signed `INT_MIN`, was caught by UBSan.

## 5. Splitting text: `strtok`, `strtok_r`, `strsep`

`strtok(s, delims)` returns the first token of `s` and remembers where it stopped in **a hidden static variable**; later calls with `NULL` continue from there. Three consequences, each shown below:

1. **It cannot be nested.** An inner `strtok` loop overwrites the outer loop's saved position.
2. **It modifies its input**: the delimiter after each token is overwritten with `'\0'`.
3. **It is not thread-safe**, because the saved position is shared by all threads.

```c
#include <stdio.h>
#include <string.h>

int main(void)
{
    char s1[] = "a=1;b=2;c=3";
    printf("strtok, nested:\n");
    for (char *pair = strtok(s1, ";"); pair; pair = strtok(NULL, ";")) {
        char *key = strtok(pair, "=");
        char *val = strtok(NULL, "=");
        printf("  %s -> %s\n", key, val);
    }

    char s2[] = "a=1;b=2;c=3";
    char *outer, *inner;
    printf("strtok_r, nested:\n");
    for (char *pair = strtok_r(s2, ";", &outer); pair; pair = strtok_r(NULL, ";", &outer)) {
        char *key = strtok_r(pair, "=", &inner);
        char *val = strtok_r(NULL, "=", &inner);
        printf("  %s -> %s\n", key, val);
    }

    char s3[] = "x,y";
    strtok(s3, ",");
    printf("the input after strtok: s3[1] is %s\n", s3[1] == '\0' ? "a terminator now (the comma was overwritten)" : "still a comma");
    return 0;
}
```

```text
strtok, nested:
  a -> 1
strtok_r, nested:
  a -> 1
  b -> 2
  c -> 3
the input after strtok: s3[1] is a terminator now (the comma was overwritten)
```

The first loop was meant to print three pairs and printed one: the inner `strtok(pair, "=")` calls replaced the saved position of the outer loop, so the next outer `strtok(NULL, ";")` continued from the end of `"a=1"` and found nothing. `strtok_r` takes the saved position as an argument (`&outer`, `&inner`) and printed all three. The last line shows the input being destroyed. Since `strtok` writes into the string, it cannot be given a literal:

```c
#include <stdio.h>
#include <string.h>

int main(void)
{
    char *s = "red green blue";
    char *t = strtok(s, " ");
    printf("first token: %s\n", t);
    return 0;
}
```

```text
Segmentation fault      ./p3b_lit
exit status 139
```

The program died with a segmentation fault (exit status 139 is 128 plus signal 11) on the first `strtok`, because `"red green blue"` lives in read-only memory. Declare the string as `char s[] = "..."` to get a writable copy.

Two other points. `strtok` treats a run of delimiters as one, so `"a,,b"` gives two tokens, not three: the empty field is lost, which makes it unsuitable for CSV. `strsep` (BSD, glibc; not standard C) returns empty tokens and keeps its state in the pointer you give it. The reentrant version below is `xstrtok_r`, with the same contract as POSIX `strtok_r`, tested token for token against the library on random inputs; its listing is in section 4 (`parsefn`).

### 5.1 Functions that return a static buffer

`strtok` is one of a family. `ctime`, `asctime`, `localtime`, `gmtime`, `strerror` in older libraries, `inet_ntoa` and `getenv` return pointers to storage the library owns, which a later call may overwrite. Two calls in one expression, or two threads, collide:

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void)
{
    setenv("TZ", "UTC", 1);
    tzset();
    time_t t1 = 0, t2 = 86400L * 365;
    char *a = ctime(&t1);
    char *b = ctime(&t2);
    printf("ctime twice: same pointer: %s\n", a == b ? "yes" : "no");
    printf("  first result now reads:  %.24s\n", a);
    printf("  second result reads:     %.24s\n", b);
    char r1[32], r2[32];
    ctime_r(&t1, r1);
    ctime_r(&t2, r2);
    printf("ctime_r into own buffers:\n  %.24s\n  %.24s\n", r1, r2);
    return 0;
}
```

```text
ctime twice: same pointer: yes
  first result now reads:  Fri Jan  1 00:00:00 1971
  second result reads:     Fri Jan  1 00:00:00 1971
ctime_r into own buffers:
  Thu Jan  1 00:00:00 1970
  Fri Jan  1 00:00:00 1971
```

Both calls returned the same pointer, so after the second call the first "result" reads the second date (1971 twice). The `_r` versions write into a buffer you provide and are the fix. The same family explains most "works in testing, fails under load" reports from code that called one of these functions from two threads.

## 6. `qsort` and `bsearch`

`qsort(base, n, size, cmp)` sorts any array; `cmp` receives pointers **to the elements**. Four rules:

1. For an array of `int`, `cmp` gets `const int *`; for an array of `char *`, it gets `char **`. Passing `strcmp` directly is a type error that the compiler cannot see through `const void *`.
2. Return negative, zero or positive; do not return `a - b` for integers, because the subtraction overflows for operands of opposite sign near the limits (undefined behaviour for `int`). Use `(a > b) - (a < b)`.
3. The comparison must be a consistent total order. An inconsistent one (for example comparing floats with NaN) gives an unspecified order and in a few implementations an out-of-bounds access.
4. `qsort` is not required to be stable, and `bsearch` on duplicates may return any of them.

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int main(void)
{
    int v[] = {40, -5, 2147483647, 7, -2147483647 - 1, 7, 0};
    size_t n = sizeof v / sizeof v[0];
    qsort(v, n, sizeof v[0], cmp_int);
    printf("sorted ints:");
    for (size_t i = 0; i < n; ++i)
        printf(" %d", v[i]);
    printf("\n");
    int key = 7, miss = 8;
    int *f = bsearch(&key, v, n, sizeof v[0], cmp_int);
    printf("bsearch 7: %s (index %td, one of the two 7s);  bsearch 8: %s\n", f ? "found" : "missing", f ? f - v : -1, bsearch(&miss, v, n, sizeof v[0], cmp_int) ? "found" : "missing");
    const char *names[] = {"pear", "apple", "fig", "banana"};
    qsort(names, 4, sizeof names[0], cmp_str);
    printf("sorted strings:");
    for (int i = 0; i < 4; ++i)
        printf(" %s", names[i]);
    printf("\n");
    return 0;
}
```

```text
sorted ints: -2147483648 -5 0 7 7 40 2147483647
bsearch 7: found (index 3, one of the two 7s);  bsearch 8: missing
sorted strings: apple banana fig pear
```

The extremes `INT_MIN` and `INT_MAX` sorted correctly with the subtraction-free comparator. `bsearch` found a 7 at index 3 (either of the two 7s would be correct) and reported 8 as missing. Both functions call `cmp` through a pointer, so they are slower than an inlined sort in C++ or a hand-written one for a fixed type (from memory).

## 7. stdio buffering

`printf` does not write to the terminal or the file. It copies text into a buffer owned by the `FILE`, and the C library writes the buffer to the kernel with `write` when a rule says so. There are three rules, set per stream:

| Mode | Flushes when | Default for |
|---|---|---|
| Fully buffered (`_IOFBF`) | the buffer is full (a block size such as 4096), on `fflush`, on `exit`, on `fclose` | files and pipes |
| Line buffered (`_IOLBF`) | a newline is written, the buffer is full, on `fflush`, on input from a terminal | a terminal (from memory; a container without a terminal cannot show this) |
| Unbuffered (`_IONBF`) | every call | `stderr` |

`setvbuf(fp, buf, mode, size)` changes the mode, and must be called before any other operation on the stream; a buffer you supply must outlive the stream. This program writes 1000 lines under each mode, and `strace` counted its `write` calls (`strace -e trace=write`, stdout redirected to `/dev/null` or a pipe):

```c
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    int mode = _IOFBF;
    if (argc > 1 && !strcmp(argv[1], "line")) mode = _IOLBF;
    if (argc > 1 && !strcmp(argv[1], "none")) mode = _IONBF;
    if (argc > 1 && strcmp(argv[1], "default")) setvbuf(stdout, NULL, mode, 0);
    for (int i = 0; i < 1000; ++i)
        printf("line %04d\n", i);
    return 0;
}
```

```text
stdout to /dev/null, no setvbuf call:  3 write calls for 1000 lines
stdout to a pipe, no setvbuf call:     3 write calls
setvbuf full  (to /dev/null):       3 write calls
setvbuf line  (to /dev/null):       1000 write calls
setvbuf none  (to /dev/null):       1000 write calls
```

The default (no `setvbuf`) behaved as fully buffered both to `/dev/null` and to a pipe: 10,000 bytes went out in 3 calls of up to 4096 bytes. Forcing line buffering or no buffering gave 1000 calls, one per line. A system call is the expensive part. Writing 2,000,000 lines of 13 bytes (26,000,000 bytes) to `/dev/null`:

```text
T4 2000000 lines written with each buffering mode, bytes produced: full 26000000, line 26000000, none 26000000, fflush after each line 26000000
```

| 2,000,000 lines | Time ms (run 1 / run 2) |
|---|---|
| fully buffered | 118.00 / 139.00 |
| line buffered | 433.00 / 402.00 |
| unbuffered | 411.00 / 389.00 |
| fully buffered plus `fflush` after each line | 428.00 / 400.00 |

All four produced the same bytes. Buffering was about 3 to 3.7 times faster than writing each line, and `fflush` after every line brought the cost back to the unbuffered level. This is why logging code that flushes on every message is slow, and why code that must not lose its last message before a crash (a log of an unfinished operation) pays that price on purpose.

### 7.1 Why `printf` before `fork` can print twice

`fork` copies the whole process, **including the stdio buffer**. Whatever was printed but not yet flushed is now in two buffers, and both processes will flush it:

```c
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "plain";
    if (!strcmp(mode, "newline"))
        printf("before fork\n");
    else
        printf("before fork");
    if (!strcmp(mode, "flush"))
        fflush(stdout);
    pid_t p = fork();
    if (p == 0) {
        printf(" (child)\n");
        return 0;
    }
    wait(NULL);
    printf(" (parent)\n");
    return 0;
}
```

```text
$ ./p6_fork | cat           (no newline before fork)
before fork (child)
before fork (parent)

$ ./p6_fork flush | cat     (fflush before fork)
before fork (child)
 (parent)

$ ./p6_fork newline | cat   (newline before fork, stdout is a pipe)
before fork
 (child)
before fork
 (parent)
```

- With stdout a pipe (fully buffered), `before fork` was waiting in the buffer when `fork` was called, so both the child and the parent printed it, once each: two lines.
- With `fflush(stdout)` before `fork`, the text was written once and the buffers were empty at the fork, so it appears once and each process adds its own text.
- Adding a newline did not help, because the stream is a pipe and a newline only flushes a line-buffered stream. On a terminal the newline version would print the text once (from memory; the container has no terminal). This is the classic "it prints once when I run it and twice when I redirect it" bug.

Rules: `fflush(NULL)` (flush all output streams) before `fork`; in the child after `fork`, call `_exit` rather than `exit` if it will only `exec` or leave, so the child does not flush a copy of the parent's buffer (C 06, section 6.1). The same applies to anything buffered by a library, not only stdio.

Other buffering facts worth knowing: `stderr` is unbuffered so a message appears even if the program then crashes, while buffered `stdout` text can be lost in a crash and appear in a different order from `stderr` text when both go to the same file; mixing `printf` and a direct `write(1, ...)` on the same descriptor reorders output, because only one of them is buffered; reading from a terminal flushes line-buffered output streams on glibc so prompts appear before `fgets` waits (from memory).

## 8. Bit manipulation

### 8.1 Operators, promotion and the undefined cases

The operators are `&`, `|`, `^`, `~`, `<<` and `>>`. Four rules decide most bugs:

1. **Precedence.** `==` binds tighter than `&`, `^` and `|`. `x & 1 == 0` means `x & (1 == 0)`, which is `x & 0`, which is always 0. Parenthesise every bitwise test: `(x & 1) == 0`.
2. **Integer promotion.** Operands narrower than `int` are converted to `int` before the operator runs. `~b` on a `uint8_t` is therefore a negative `int`, not an 8-bit value.
3. **Shifts.** A shift count that is negative or at least the width of the promoted left operand is undefined. A left shift that overflows a signed type, including shifting a 1 into the sign bit (`1 << 31` with `int`), is undefined in C11 and earlier. Use unsigned operands: `1u << 31`. A right shift of a negative signed value is implementation-defined (arithmetic on GCC).
4. **Use unsigned types for bit work** (`uint32_t`, `uint64_t`), whose arithmetic is defined modulo 2^N.

The demonstration program runs the toolkit of the next section and each pitfall. Its compiler warnings are shown first, because the compiler spotted the promotion trap:

```text
p8_bits.c:33:106: warning: comparison of promoted bitwise complement of an unsigned value with constant [-Wsign-compare]
```

### 8.2 The idioms, in code

```c
#define BIT(n)           (1u << (n))
#define SET_BIT(x, n)    ((x) |= BIT(n))
#define CLEAR_BIT(x, n)  ((x) &= ~BIT(n))
#define TOGGLE_BIT(x, n) ((x) ^= BIT(n))
#define TEST_BIT(x, n)   (((x) >> (n)) & 1u)

/* Kernighan: each x &= x - 1 clears the lowest set bit. */
static int popcount32(uint32_t x)
{
    int c = 0;
    while (x) {
        x &= x - 1;
        ++c;
    }
    return c;
}

static int parity32(uint32_t x)
{
    x ^= x >> 16;
    x ^= x >> 8;
    x ^= x >> 4;
    x ^= x >> 2;
    x ^= x >> 1;
    return (int)(x & 1u);
}

static uint32_t reverse32(uint32_t x)
{
    x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
    x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
    x = ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);
    x = ((x >> 8) & 0x00FF00FFu) | ((x & 0x00FF00FFu) << 8);
    return (x >> 16) | (x << 16);
}

static uint32_t bswap32(uint32_t x)
{
    return (x >> 24) | ((x >> 8) & 0x0000FF00u) | ((x << 8) & 0x00FF0000u) | (x << 24);
}

static int is_pow2(uint32_t x) { return x && !(x & (x - 1)); }

static uint32_t lowest_bit(uint32_t x) { return x & (0u - x); }

/* Smallest power of two >= x (1 for 0 and 1); 0 if it does not fit in 32 bits. */
static uint32_t round_up_pow2(uint32_t x)
{
    if (x <= 1)
        return 1;
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1;
}

/* Rotate left; any count is allowed (a shift by 32 would be undefined). */
static uint32_t rotl32(uint32_t x, unsigned r)
{
    r &= 31;
    return r ? (x << r) | (x >> (32 - r)) : x;
}

/* Treat the low `bits` bits (1..32) of v as a two's complement number. */
static int32_t sign_extend(uint32_t v, unsigned bits)
{
    uint32_t m = 1u << (bits - 1);
    if (bits < 32)
        v &= (1u << bits) - 1;
    return (int32_t)((v ^ m) - m);
}

/* Register fields, in the style of the Linux GENMASK and FIELD_GET. */
#define FIELD_MASK(hi, lo) ((uint32_t)((0xFFFFFFFFu >> (31 - (hi))) & (0xFFFFFFFFu << (lo))))
#define FIELD_GET(mask, reg) (((reg) & (mask)) >> __builtin_ctz(mask))
#define FIELD_PREP(mask, val) (((uint32_t)(val) << __builtin_ctz(mask)) & (mask))
#define FIELD_SET(reg, mask, val) ((reg) = ((reg) & ~(mask)) | FIELD_PREP(mask, val))
```

How each one works:

- **Set, clear, toggle, test.** OR with a mask sets, AND with the inverted mask clears, XOR toggles, shift and AND tests. The macros use `1u`, so the mask is unsigned. They are for a variable, not for a hardware register, which needs a `volatile` pointer and a read-modify-write (C 05).
- **Clear the lowest set bit: `x & (x - 1)`.** Subtracting 1 flips the lowest set bit and every zero below it, so ANDing clears exactly that bit. Repeating it until zero counts the set bits (`popcount32`, Kernighan's method; it loops once per set bit) and tests for a power of two (`x && !(x & (x - 1))`).
- **Isolate the lowest set bit: `x & -x`** (written `x & (0u - x)` to stay in unsigned arithmetic). In two's complement, `-x` is `~x + 1`: everything above the lowest set bit is inverted and the lowest set bit stays.
- **Round up to a power of two.** Subtract 1, smear the highest set bit into every lower position with shifts of 1, 2, 4, 8, 16, then add 1. Subtracting first keeps exact powers of two unchanged (a deliberate bug that skipped it was caught). The result for inputs above 2^31 does not fit and is 0.
- **Reverse.** Swap adjacent bits, then pairs, then nibbles, then bytes, then halves: five masked swaps instead of a 32 iteration loop.
- **Rotate.** `(x << r) | (x >> (32 - r))`, with the count reduced mod 32 first, because a count of 32 or 0 would shift by 32, which is undefined. GCC and Clang recognise the pattern and emit one `rol` instruction (from memory).
- **Parity.** XOR-fold the word in half repeatedly until one bit remains.
- **Sign extension.** To treat the low `bits` of a value as a signed number, XOR with the sign bit and subtract it: `(v ^ m) - m`. A 12-bit field `0x800` becomes -2048. The test checks every width from 1 to 32. (The final conversion of an out-of-range unsigned value to `int32_t` is implementation-defined in C and wraps on GCC.)
- **Fields.** `FIELD_MASK(hi, lo)` builds the mask of bits `lo` to `hi`; `FIELD_GET` masks and shifts down by the position of the lowest set bit (`__builtin_ctz`); `FIELD_PREP` shifts a value up and masks it so an oversized value cannot spill into neighbouring fields; `FIELD_SET` is a read-modify-write that leaves the other bits alone. These are the same ideas as `GENMASK`, `FIELD_GET` and `FIELD_PREP` in the Linux kernel (from memory), and they are the safe way to describe hardware registers: bit-field layout in a `struct` is implementation-defined (C 04), a mask is not.

```text
masks: EN=0x00000001 MODE=0x0000000e DIV=0x0000ff00
ctrl = 0xffff2a0b  (upper half untouched)
read back: EN=1 MODE=5 DIV=0x2a
popcount(0xF0F0F0F0)=16  parity(7)=1  reverse32(1)=0x80000000  bswap32(0x12345678)=0x78563412
is_pow2: 64 -> 1, 96 -> 0, 0 -> 0
lowest_bit(0x1A0)=0x20  round_up_pow2(1000)=1024  round_up_pow2(1024)=1024
rotl32(0x80000001, 4)=0x00000018  rotl32(x, 32) leaves x alone: yes
sign_extend(0xFF, 8)=-1  sign_extend(0x7F, 8)=127  sign_extend(0x800, 12)=-2048

pitfalls
  uint8_t b = 0x0F:  ~b = 0xfffffff0,  (~b == 0xF0) is 0,  ((uint8_t)~b == 0xF0) is 1
  x = 6:  (x & 1 == 0) is 0,  ((x & 1) == 0) is 1
  -128 >> 4 as signed char: -8 (GCC: arithmetic shift, implementation-defined);  0x80 >> 4 as unsigned char: 8
  char 0xFF widened: (int)ch = -1,  (unsigned char)ch = 255,  ch & 0xFF = 255
  1u << 31 = 0x80000000
  bytes of 0x01020304 in memory: 04 03 02 01 (this machine is little-endian)
  big-endian wire format by shifts: 01 02 03 04 (same on any machine)
```

Reading the output: the register `0xffff0000` kept its upper half while the three fields were set (`EN` is bit 0, `MODE` bits 3 to 1, `DIV` bits 15 to 8), giving `0xffff2a0b`; reading them back returned 1, 5 and `0x2a`. `~b` on a `uint8_t` of `0x0F` was `0xfffffff0`, so `~b == 0xF0` is false, and only after casting back to `uint8_t` is it true. `(x & 1 == 0)` was 0 for `x = 6` although 6 is even. A `char` holding `0xFF` widened to -1, and the idiom to get 255 is the cast to `unsigned char` or `& 0xFF`.

### 8.3 Endianness and wire formats

On a little-endian machine (x86-64 and most ARM64 systems) the byte at the lowest address is the **least** significant: `0x01020304` is stored as `04 03 02 01`, as the output showed. A network protocol or file format that specifies big-endian must be written and read **by shifts**, not by casting a pointer or copying the integer: the shift code gives `01 02 03 04` on every machine and does not depend on alignment or strict aliasing (C 05). `bswap32` converts between the two orders, and `htonl` and `ntohl` do it for network byte order (C 04's checksum problem uses the same ideas).

### 8.4 Counting bits: what the instruction set changes

The loop above takes one iteration per set bit. A table of 256 counts takes four lookups, and the hardware has a `popcnt` instruction. 50,331,648 random values:

```text
T3 popcount of 50331648 values: sums kernighan 805163248, table 805163248, builtin 805163248, builtin with -mpopcnt 805163248; popcnt instructions in the binary: 0 default build, 3 with -mpopcnt
```

| 50,331,648 values | Time ms (run 1 / run 2) |
|---|---|
| Kernighan loop (`popcount32`) | 1046.00 / 1065.00 |
| table of 256, four lookups | 58.00 / 58.00 |
| `__builtin_popcount`, default build | 89.00 / 93.00 |
| `__builtin_popcount` with `-mpopcnt` | 26.00 / 25.00 |

The loop averaged about 16 iterations per value (half of 32 bits) and was about 18 times slower than the table. The default build of `__builtin_popcount` contains **no** `popcnt` instruction: the baseline x86-64 target does not guarantee one, so GCC calls a library routine that is table based, and the time is close to my table. With `-mpopcnt` the compiler emitted the instruction (3 occurrences) and it became the fastest, about 2.2 times faster than the table. The lesson is not "use the builtin": it is that **the same source is about 3.5 times faster when the build target allows the instruction**, which is why `-march=native` or a defined minimum CPU matters in performance work, and why portable code uses the builtin (which picks the best available) instead of its own loop. The other builtins are `__builtin_clz`, `__builtin_ctz` (both **undefined for 0**, per the GCC manual), `__builtin_parity`, `__builtin_bswap32` and the `ll` variants for 64 bits; C23 adds standard `stdc_count_ones` and friends in `<stdbit.h>` (from memory).

## 9. What it costs

| Question | Answer (this machine, GCC 13.3) |
|---|---|
| `strncpy` with a much larger buffer than the string | pads the rest with zeros: 2.5 to 3 times a `strlcpy`-style copy at 4 KB |
| `strlen` in a loop condition | O(n) per iteration, O(n²) for the loop |
| Byte-loop copy against library `memcpy` | about 4 times slower (16 MiB blocks) |
| `snprintf` for a simple copy | about 44 ns per call here; parses the format each time |
| System call per line of output | 3 to 4 times the cost of buffering (2,000,000 lines to `/dev/null`) |
| `fflush` after every line | brings buffered output back to the unbuffered cost |
| Kernighan popcount against `popcnt` | about 40 times slower (1046 to 1065 ms against 25 to 26 ms) on random data |
| `__builtin_popcount` without `-mpopcnt` | a library routine, close to a table lookup |
| `qsort` comparator call per comparison | an indirect call each time; no inlining |

## 10. Common mistakes

1. **Using `strncpy` as a safe `strcpy`.** No terminator when the source fills the buffer; zero padding when it does not. Use `snprintf` or a `strlcpy`-style copy.
2. **Passing `sizeof buf` to `strncat`.** Its limit is on appended characters; the room needed is `sizeof buf - strlen(buf) - 1`.
3. **Ignoring the return value of `snprintf`.** It tells you the text was cut.
4. **`memcpy` on overlapping buffers.** Use `memmove`.
5. **`memcmp` on a struct, or `==` on a float array copied with `memcmp`.** Padding and NaN make equal-looking values differ.
6. **`memset` to wipe a secret.** The compiler may remove it. Use `explicit_bzero`.
7. **`atoi` for input that can be wrong.** Use `strtol`, set `errno = 0` first, check `endptr`, check the range.
8. **`strtok` in nested loops, in threads, or on a literal.** Use `strtok_r` on a writable copy.
9. **A comparator that returns `a - b`.** It overflows. Return `(a > b) - (a < b)`.
10. **`printf` before `fork` with unflushed output.** Flush first; use `_exit` in a child that leaves without `exec`.
11. **`x & 1 == 0`.** Parenthesise every bitwise test.
12. **`1 << 31`, or a shift by the width.** Use `1u` and keep the count below the width.
13. **`~b` on a narrow unsigned type compared with a narrow constant.** The promotion makes it a different number.
14. **Casting a pointer to read a big-endian field.** Assemble the value with shifts.

## 11. Questions out loud

1. **Why is `strncpy` unsafe?** It does not terminate when the source is as long as the limit, and it pads with zeros up to the limit when the source is shorter, which is a cost and a surprise.
2. **How do you copy a string safely?** `snprintf(dst, size, "%s", src)` or a `strlcpy`-style function, then compare the returned length with `size` to detect truncation.
3. **What is the difference between `memcpy` and `memmove`?** `memcpy` may assume the regions are disjoint and may copy in any order; `memmove` handles overlap by choosing the copy direction from the relative addresses.
4. **How would you write `memmove`?** If `dst` is below `src` copy forward, else copy backward, comparing addresses through `uintptr_t`; handle `n == 0` and `dst == src`.
5. **Why not `atoi`?** It cannot report an error, and overflow is undefined. `strtol` gives `endptr` and `errno`.
6. **How do you write a safe string-to-int?** `strtol` with `errno = 0`, check `endptr != s`, check the terminator, check `ERANGE`, and check the range of the destination type.
7. **How do you write `itoa`?** Peel digits with `% base` into a temporary buffer, reverse them, and negate in unsigned arithmetic so `INT_MIN` works.
8. **Why is `strtok` not thread-safe, and what else is wrong with it?** Hidden static position; it modifies its input, fails on literals, cannot nest and merges empty fields. Use `strtok_r`.
9. **What are the stdio buffering modes?** Full (files, pipes), line (terminals), none (`stderr`). `setvbuf` changes them, before the first I/O.
10. **Why can `printf` before `fork` print twice?** `fork` copies the unflushed buffer into the child; both flush it. `fflush` before `fork`.
11. **What does `x & (x - 1)` do?** Clears the lowest set bit; it counts bits and tests for a power of two.
12. **How do you count set bits?** Kernighan loop (one iteration per set bit), a table, or `__builtin_popcount`, which becomes one instruction when the target has `popcnt`.
13. **How do you check for a power of two?** `x && !(x & (x - 1))`.
14. **How do you write to a hardware register field?** Read, clear the field mask, OR in the prepared value, write back, through a `volatile` pointer; use masks and shifts, not struct bit-fields.
15. **Why is `1 << 31` a problem?** It shifts into the sign bit of an `int`, which is undefined in C11 and earlier; use `1u << 31`.
16. **How do you read a 32-bit big-endian integer from a byte buffer?** `(b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]` with each byte widened to `uint32_t` first; no pointer cast.

---

## Problems

Difficulty is marked E (entry level), M (medium) or H (hard). Do A and B on paper first.

**A (E, on paper).** What does each line of this program print? (`x` is `unsigned`; `rotl32` rotates left; `FIELD_GET(mask, v)` extracts a field; `FIELD_PREP(mask, v)` places a value.)

```text
char t[4];
int r = snprintf(t, sizeof t, "%d", 12345);            // 1: print t and r
char s[8] = "ab"; strncat(s, "cdefgh", 2);             // 2: print s
unsigned x = 0xB4;                                      // 3: popcount(x), x & -x, x & (x - 1),
                                                        //    x >> 2 & 3, rotl32(x, 28)
sign_extend(0xB4, 8)                                    // 4
FIELD_GET(FIELD_MASK(7, 4), 0xB4u), FIELD_PREP(FIELD_MASK(11, 8), 5)   // 5
```

**B (E, on paper).** This program is run with stdout redirected to a pipe (`./prog | cat`). What does it print, and how would the output differ on a terminal?

```text
printf("a\n");
pid_t p = fork();
if (p == 0) { printf("b\n"); return 0; }
wait(NULL);
printf("b\n");
```

**C (M, in your editor).** Write `xstrlcat(dst, src, size)` (append, truncate, always terminate, return the length it tried to create) and `path_join(dst, size, dir, name)` built on it that inserts exactly one `/` between the parts. Test against `snprintf` for sizes 0, 1, 2 and larger, and for `dir` ending in `/` and not. Run under ASan.

**D (M, in your editor).** A status register has fields `READY` (bit 0), `ERR` (bits 3 to 1), `COUNT` (bits 15 to 8, unsigned) and `TEMP` (bits 27 to 20, signed two's complement, in degrees). Write masks, getters and setters with the field macros, a `_Static_assert` that no two masks overlap, and a function that returns `TEMP` as an `int`. Test with random register values against a version written with plain shifts.

**E (H, in your editor).** Write `split_fields(char *line, char sep, char **out, size_t max)` that splits a line into fields in place, **keeps empty fields** (`"a,,b"` gives three), and treats a field in double quotes as one field even if it contains `sep`. It must not use `strtok`, must be reentrant, and must report how many fields it found (or that `max` was too small). Write the tests first, and include `""`, `","`, `",,"` and an unterminated quote.

**Where to solve them.** A and B on paper, then check against the answers. C, D, E in your editor with `gcc -std=gnu11 -Wall -Wextra -fsanitize=address,undefined`. The listings in sections 2 to 5 and 8 are the style to follow.

**How to approach them.**

- **A:** 1: `snprintf` returns the length it wanted, not what fit. 2: the third argument of `strncat` counts appended characters. 3: write 0xB4 in binary (1011 0100) and apply each expression; remember `>>` binds tighter than `&`. 4: bit 7 is set. 5: the masks are bits 7 to 4 and bits 11 to 8.
- **B:** list what each process has in its buffer at the moment of the fork, and when each one flushes. Then ask what changes if the stream is line buffered.
- **C:** measure `strlen(dst)` first, handle `size <= len` (nothing fits), and compute the return as `len + strlen(src)`; do the `/` logic before the second append.
- **D:** build each mask with `FIELD_MASK`; for `TEMP` get the field, then sign-extend 8 bits. Static-assert with `(A & B) == 0` for every pair.
- **E:** write a state machine with two states (in quotes, not in quotes); decide first what happens to the quote characters (strip them, and say so).

## Answers

**A.** The real output (the compiler warns about line 1, correctly):

```text
1. snprintf: t = "123", returned 5
2. strncat: abcd
3. popcount=4  x & -x = 0x4  x & (x-1) = 0xb0  x >> 2 & 3 = 1  rotl32(x, 28) = 0x4000000b
4. sign_extend(0xB4, 8) = -76
5. FIELD_GET(FIELD_MASK(7, 4), 0xB4) = 11  FIELD_PREP(FIELD_MASK(11, 8), 5) = 0x500
```

1. `snprintf` wrote `123` and the terminator (3 characters fit in 4 bytes) and returned 5, the length of `12345`: truncation is visible only through the return value. 2. `strncat` appended 2 characters, so `abcd`. 3. `0xB4` is `1011 0100`: 4 bits set; `x & -x` is the lowest set bit, 4; `x & (x - 1)` clears it, `0xb0`; `x >> 2 & 3` is `(0xB4 >> 2) & 3`, which is `0x2D & 3`, which is 1; rotating left by 28 is rotating right by 4, `0x4000000b`. 4. The top bit of the 8-bit value is set, so it is `0xB4 - 256`, which is -76. 5. Bits 7 to 4 of `0xB4` are `0xB`, which is 11; 5 placed at bit 8 is `0x500`.

**B.** The real output with stdout a pipe:

```text
a
b
a
b
```

Both processes had `a\n` in their buffers at the fork (a pipe is fully buffered, so the newline did not flush it). The child added `b\n` and flushed at exit, so the pipe received `a`, `b`. The parent waited, added its own `b\n`, and flushed `a`, `b`. On a terminal (line buffered) `a` is written immediately, before the fork, so the output is `a`, `b`, `b` (from memory; I could not run it on a terminal here).

**C.** Outline. `xstrlcat`: `size_t dl = strnlen(dst, size);` (not `strlen`, in case `dst` is unterminated); `if (dl == size) return size + strlen(src);` (no room; no terminator found), else `return dl + xstrlcpy(dst + dl, src, size - dl);`. `path_join` copies `dir`, appends `/` only if `dir` is non-empty and does not already end in `/`, then appends `name`; return the total length it wanted so the caller can compare it with `size`. The oracle is `snprintf(ref, size, "%s%s%s", dir, sep, name)`, which has the same truncation and return value; compare the return value and the whole buffer, including the bytes after the terminator, which must not change.

**D.** Outline. `#define ST_READY FIELD_MASK(0, 0)`, `ST_ERR FIELD_MASK(3, 1)`, `ST_COUNT FIELD_MASK(15, 8)`, `ST_TEMP FIELD_MASK(27, 20)`. `_Static_assert((ST_READY & ST_ERR) == 0 && ...)` for the six pairs (the masks are integer constant expressions here, so it compiles). `int temp(uint32_t r) { return sign_extend(FIELD_GET(ST_TEMP, r), 8); }`. The reference test builds the same values with `(r >> 20) & 0xFF`, then `if (v & 0x80) v -= 256`. `FIELD_PREP` works for a negative temperature, because its final mask discards the sign bits above the field and keeps the low 8 bits, which are the two's complement value. The trap is the other direction: a value outside -128 to 127 is silently truncated, so range-check it before writing.

**E.** Outline. Walk the line with a read pointer and a write pointer into the same buffer (the write pointer never passes the read pointer, so in-place is safe). In the unquoted state, a `sep` ends the field (write `'\0'`, record the start, begin the next field even when it is empty), and a `"` enters the quoted state (do not copy it). In the quoted state copy characters until the closing `"`, which returns to the unquoted state. At the end of the line record the last field (so `","` gives two empty fields and `""` gives one empty field). An unterminated quote should be an error, not silently accepted. When `max` is reached, keep counting without storing and return the full count so the caller can detect it. Compare `strtok` on `"a,,b"`: two tokens, the empty field lost.

## Cheat sheet

- **String:** array of `char` ending at the first NUL; capacity is `sizeof`, length is `strlen` (O(n)); literals are read-only.
- **Bounded copy:** `snprintf` (returns the wanted length) or `strlcpy` (glibc 2.38 and later, BSDs); never `strncpy` as a safe `strcpy`; `strncat` limits appended characters only.
- **`memcpy`** disjoint only; **`memmove`** any overlap (direction from the addresses, via `uintptr_t`); call the library, not a byte loop; **`memcmp`** not for structs; **`explicit_bzero`** for secrets.
- **Numbers:** `strtol` with `errno = 0`, `endptr`, `ERANGE` and a range check; `atoi` hides errors; `strtoul("-1")` wraps; `itoa` negates in unsigned arithmetic.
- **Tokens:** `strtok` has hidden state, edits its input, merges empty fields; `strtok_r` or `strsep` instead; use a writable array, not a literal.
- **Static buffers:** `ctime`, `asctime`, `localtime`, `strtok`: use the `_r` versions.
- **`qsort`:** comparator gets pointers to elements; return `(a > b) - (a < b)`; not stable.
- **stdio:** full (files, pipes), line (terminals), none (`stderr`); `setvbuf` before first use; `fflush(NULL)` before `fork`; `_exit` in a child that does not `exec`; flush on every line costs a system call per line.
- **Bits:** `|` set, `& ~` clear, `^` toggle, `(x >> n) & 1` test; `x & (x - 1)` clears the lowest set bit; `x & -x` isolates it; `x && !(x & (x - 1))` power of two; always `1u`.
- **Pitfalls:** `x & 1 == 0`; `~` on narrow types; shift count at least the width; `1 << 31`; right shift of negative; `char` signedness.
- **Fields:** mask and shift, read-modify-write, `volatile` for hardware; masks, not struct bit-fields, for registers.
- **Endianness:** build and parse wire formats with shifts; `bswap`, `htonl`, `ntohl`.
- **Builtins:** `__builtin_popcount`, `clz`, `ctz` (undefined for 0), `bswap32`, `parity`; add `-mpopcnt` or `-march` to get the instruction.

## Sources

- [strncpy(3), Linux man pages](https://man7.org/linux/man-pages/man3/strncpy.3.html): the padding and missing terminator, and the history of the function.
- [strtol(3), Linux man pages](https://man7.org/linux/man-pages/man3/strtol.3.html): `endptr`, `ERANGE`, bases, white space and sign handling.
- [strtok(3), Linux man pages](https://man7.org/linux/man-pages/man3/strtok.3.html): the static state, the modification of the input, `strtok_r` and `strsep`.
- [setbuf(3), Linux man pages](https://man7.org/linux/man-pages/man3/setbuf.3.html): `setvbuf`, the three buffering modes and their defaults.
- [GCC manual: Bit Operation Builtins](https://gcc.gnu.org/onlinedocs/gcc/Bit-Operation-Builtins.html): `__builtin_popcount`, `clz`, `ctz`, `bswap`, `parity`, and the undefined result for 0.
- [Sean Eron Anderson, Bit Twiddling Hacks](https://graphics.stanford.edu/~seander/bithacks.html): the origin of most of the idioms in section 8.
- [Miller and de Raadt, strlcpy and strlcat: consistent, safe, string copy and concatenation (USENIX 1999)](https://www.usenix.org/legacy/event/usenix99/full_papers/millert/millert.pdf): the design of `strlcpy`.
