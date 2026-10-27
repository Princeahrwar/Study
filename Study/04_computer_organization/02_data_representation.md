# CO 02: Data Representation

*Part of the [Computer Organization plan](00_plan.md) · Time: about 3 hours (1.5 reading, 1.5 problems) · You need: [tutorial 01](01_how_a_computer_works.md) and basic C*

By the end of this tutorial you can:

- convert between binary, hexadecimal and decimal, and read hex dumps;
- write negative numbers in two's complement and explain why every CPU uses it;
- tell **carry** (unsigned overflow) from **overflow** (signed overflow), and know what C promises about each;
- predict what shifts, casts and mixed signed/unsigned comparisons do;
- explain byte order, and decode a value from raw bytes correctly;
- decode and encode an IEEE-754 float by hand, and explain why `0.1 + 0.2 != 0.3`;
- explain how text is stored (ASCII, UTF-8), and work out a struct's size from its alignment rules.

---

## 1. The one big idea: bits have no meaning until a type gives them one

Memory holds only bits. The same 32 bits can be an `int`, an `unsigned`, a `float`, four characters, or an instruction. Nothing in the bits says which. **The type you use to read them decides the meaning.**

```text
bits:  0100 0001 0100 1000 0000 0000 0000 0000      (0x41480000)
as unsigned int   ->  1,095,237,632
as float          ->  12.5
as 4 separate bytes (most significant first) -> 'A'  'H'  0x00  0x00      (in memory the order depends on endianness, section 8)
```

Everything in this tutorial is about those rules: how numbers, text and fractions are turned into bits, and what happens at the edges (overflow, rounding, byte order).

---

## 2. Bits, bytes and number bases

A **bit** is 0 or 1. A **byte** is 8 bits. A **word** is the natural chunk size of a CPU (32 or 64 bits today). With *n* bits you can make **2ⁿ** different patterns: 8 bits give 256, 16 bits give 65,536, 32 bits give about 4.3 billion.

Numbers are written in **positional notation**: each digit is worth its base raised to its position (counting from 0 at the right).

```text
decimal  4 7 2   = 4×10² + 7×10¹ + 2×10⁰ = 472
binary   1 0 1 1 = 1×2³ + 0×2² + 1×2¹ + 1×2⁰ = 8 + 2 + 1 = 11
```

The rightmost bit is the **least significant bit (LSB, bit 0)**; the leftmost is the **most significant bit (MSB)**.

### Hexadecimal

Binary is long, so we write it in **hex** (base 16, digits 0 to 9 then A to F). The point: **one hex digit is exactly 4 bits**, so you convert by grouping.

| Hex | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | A | B | C | D | E | F |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Binary | 0000 | 0001 | 0010 | 0011 | 0100 | 0101 | 0110 | 0111 | 1000 | 1001 | 1010 | 1011 | 1100 | 1101 | 1110 | 1111 |

```text
1100 1000  ->  C 8      -> 0xC8 = 12×16 + 8 = 200
0xA3F      ->  1010 0011 1111
0x1F4      =   1×256 + 15×16 + 4 = 500
```

Octal (base 8, 3 bits per digit) survives mainly in Unix file permissions (`chmod 755`). The prefix `0x` means hex, and `0b` binary (C23, C++14 and compiler extensions).

### Decimal to binary

Divide by 2 and collect the remainders from last to first, or subtract powers of two from the largest down.

```text
200:  200 - 128 = 72   (bit 7)
       72 -  64 =  8   (bit 6)
        8 -   8 =  0   (bit 3)
      bits 7, 6, 3 set  ->  1100 1000  =  0xC8
```

### Powers of two worth knowing by heart

| 2¹⁰ | 2²⁰ | 2³⁰ | 2⁴⁰ | 2³² | 2⁶⁴ |
|---|---|---|---|---|---|
| 1,024 (KiB) | about 1 million (MiB) | about 1 billion (GiB) | about 1 trillion (TiB) | 4,294,967,296 | about 1.8 × 10¹⁹ |

A 12-bit offset addresses 4 KiB (2¹²), which is why you see 4 KiB pages. A 48-bit address reaches 256 TiB.

---

## 3. Unsigned integers

An *n*-bit unsigned integer holds 0 to 2ⁿ − 1. Addition is ordinary binary addition, with one catch: the result must still fit in *n* bits. A carry out of the top bit is **thrown away**, so the arithmetic is **modulo 2ⁿ**, like a car's odometer rolling over.

```text
  1100 1000   (200)
+ 0110 0100   (100)
-----------
1 0010 1100   -> 9 bits. Keep the low 8: 0010 1100 = 44.  The dropped 1 is the carry.
```

So `200 + 100` in 8 bits is 44, which is 300 − 256. In C, `unsigned` arithmetic is **defined** to wrap like this, so it is safe to rely on.

The wrap-around also works backwards: `0 - 1` in 32-bit unsigned is 4,294,967,295 (all ones, `0xFFFFFFFF`). This is the source of the classic loop bug:

```c
for (unsigned i = n - 1; i >= 0; i--) { ... }   // never ends: i >= 0 is always true, i wraps to 4294967295
```

---

## 4. Signed integers: two's complement

We need negative numbers too. Three schemes have been used. Here they are for 4 bits:

| Value | Sign-magnitude | One's complement | Two's complement |
|---|---|---|---|
| +3 | 0011 | 0011 | 0011 |
| +1 | 0001 | 0001 | 0001 |
| +0 | 0000 | 0000 | 0000 |
| -0 | 1000 | 1111 | (does not exist) |
| -1 | 1001 | 1110 | 1111 |
| -2 | 1010 | 1101 | 1110 |
| -3 | 1011 | 1100 | 1101 |
| -7 | 1111 | 1000 | 1001 |
| -8 | (does not exist) | (does not exist) | 1000 |

- **Sign-magnitude** puts a sign bit in front of the magnitude. Easy to read, but it has two zeros and needs special logic to add.
- **One's complement** negates by flipping every bit. Still two zeros, and carries have to be added back around ("end-around carry").
- **Two's complement** negates by **flipping every bit and adding 1**. It has one zero, and **the same adder circuit works for signed and unsigned numbers**, so subtraction is just addition of the negation. Every mainstream CPU uses it.

### How two's complement works

The MSB is worth **−2ⁿ⁻¹** instead of +2ⁿ⁻¹; all other bits keep their normal weights.

```text
8-bit 1111 0110 :  -128 + 64 + 32 + 16 + 0 + 4 + 2 + 0  =  -128 + 118  =  -10
8-bit 0000 1010 :                          8 + 2        =   10
```

**To negate:** flip all bits, add 1.

```text
  37 = 0010 0101
flip = 1101 1010
 +1  = 1101 1011  = 0xDB = -37     (check: -128 + 64 + 16 + 8 + 2 + 1 = -37)
```

**Range** for *n* bits: −2ⁿ⁻¹ to 2ⁿ⁻¹ − 1. For 8 bits that is −128 to +127; for 32 bits −2,147,483,648 to +2,147,483,647. The range is **lopsided**: there is one more negative number than positive. So `-INT_MIN` and `abs(INT_MIN)` cannot be represented (they overflow), a bug seen in real code.

**Quick check:** if the MSB is 1 the number is negative; to read its size, negate it and read the result.

### Changing the width

**Sign extension** (widening a signed value): copy the sign bit into the new upper bits. This keeps the value the same.

```text
8-bit  1011 0100  (-76)   ->  16-bit  1111 1111 1011 0100  (0xFFB4, still -76)
8-bit  0011 0100  (+52)   ->  16-bit  0000 0000 0011 0100  (0x0034, still +52)
```

**Zero extension** (widening an unsigned value): fill with 0s. Widening a `char`/`uint8_t` to `int` uses the signedness of the *source* type.

**Truncation** (narrowing): drop the upper bits. The value changes unless it fits: `(uint8_t)0x1234` is `0x34`.

### The C traps that follow

```c
int i = -1;
unsigned u = 0;
if (i < u) { ... }        // FALSE: i is converted to unsigned (4294967295), which is not < 0

char c = 0xB4;            // plain char is signed on x86 (c is -76, so int x = c gives -76, not 180)
                          // but unsigned on ARM Linux (c is 180). Use uint8_t when you mean a byte.
```

Rule: when a signed and an unsigned type of the same size meet in an expression, the **signed one becomes unsigned**. Compile with `-Wall -Wextra`; the compiler warns about most of these.

---

## 5. Overflow and carry

Both mean "the result did not fit", but for different readings of the bits. A CPU computes **both** on every addition and keeps them as flag bits (**CF** carry flag, **OF** overflow flag); software chooses which to look at.

| Name | When | Meaning | Rule |
|---|---|---|---|
| **Carry (CF)** | unsigned | the result needs more than *n* bits | a carry came out of the top bit |
| **Overflow (OF)** | signed | the result is outside −2ⁿ⁻¹…2ⁿ⁻¹−1 | two numbers of the **same sign** gave a result of the **opposite sign** |

Adding numbers of different signs can never overflow. Four 8-bit examples:

| Addition | Result bits | Unsigned reading | Signed reading | CF | OF |
|---|---|---|---|---|---|
| 0x7F + 0x01 | 0x80 | 127+1=128 fits | 127+1=128 does **not** fit (shows -128) | 0 | **1** |
| 0x80 + 0x80 | 0x00 | 128+128=256 does **not** fit | -128 + -128 = -256 does not fit | **1** | **1** |
| 0xF0 + 0x20 | 0x10 | 240+32=272 does **not** fit | -16 + 32 = 16 fits | **1** | 0 |
| 0x50 + 0x30 | 0x80 | 80+48=128 fits | 80+48=128 does **not** fit | 0 | **1** |

Third row: the same bit pattern is a perfectly good signed answer and a wrong unsigned one. That is why the hardware reports both.

**What C promises:**

- `unsigned` overflow **wraps** (defined, section 3).
- **`signed` overflow is undefined behaviour.** The compiler may assume it never happens. So `if (x + 1 > x)` can be optimised to `true`, and a check written *after* the overflow can be deleted. To check safely, test **before** adding (`if (x > INT_MAX - 1)`) or use `__builtin_add_overflow`.

---

## 6. Bitwise operations and shifts

| Op | C | Example (8-bit) | Common use |
|---|---|---|---|
| AND | `a & b` | `1100 1010 & 0000 1111 = 0000 1010` | **mask**: keep some bits, clear others |
| OR | `a \| b` | `1100 0000 \| 0000 0011 = 1100 0011` | **set** bits |
| XOR | `a ^ b` | `1100 1010 ^ 0000 1111 = 1100 0101` | **toggle** bits; swap; parity |
| NOT | `~a` | `~0000 1111 = 1111 0000` | flip all bits |

Setting, clearing, toggling and testing bit *k*:

```c
x |=  (1u << k);     // set
x &= ~(1u << k);     // clear
x ^=  (1u << k);     // toggle
if (x & (1u << k))   // test
```

Register fields in hardware and protocol headers are packed bits, read with a shift and a mask: `(reg >> 4) & 0xF` extracts bits 7 to 4.

### Shifts

Shifting left by *k* moves bits up and fills with zeros: it **multiplies by 2ᵏ**. Shifting right comes in two kinds:

- **Logical** right shift fills with 0s. Used for unsigned values: `0xF0 >> 4` is `0x0F`.
- **Arithmetic** right shift copies the sign bit. Used for signed values: `-20 >> 2` is `-5`.

Arithmetic right shift **divides by 2ᵏ, rounding toward minus infinity**. C's `/` operator rounds toward zero. They differ for negative odd numbers:

```text
-7 >> 1  = -4        (floor of -3.5)
-7 /  2  = -3        (truncation toward zero)
```

So a compiler turns `x / 2` on a signed `int` into a shift **plus a small fix-up for negatives**, not a bare shift. For unsigned values a bare shift is exact.

In C, right-shifting a negative value is implementation-defined (arithmetic on every compiler you will meet), and shifting by the width or more (`1u << 32` on a 32-bit type) is **undefined behaviour**.

Two bit tricks that appear everywhere (more in the DSA bit-manipulation tutorial):

```c
x & (x - 1)     // clears the lowest set bit: 0b101100 -> 0b101000. Zero means "x had at most one bit set"
x & -x          // isolates the lowest set bit: 0b101100 -> 0b000100
```

---

## 7. Other encodings you will run into

**Fixed-point.** An integer with an implied binary point, used where there is no floating-point unit or where exact, fast arithmetic is wanted (DSP, embedded, money in cents). In **Q15** a 16-bit signed integer represents value/2¹⁵:

```text
0.5  = 0x4000 (16384)           0.25 = 0x2000 (8192)
0.5 × 0.5: multiply the integers (16384 × 16384 = 2²⁸), then shift right by 15  ->  8192 = 0.25
```

**BCD (binary-coded decimal).** Each decimal digit in its own 4 bits, so 59 is `0101 1001` = `0x59`. Wasteful, but conversion to display is trivial; real-time-clock chips use it.

**Gray code.** Consecutive values differ in **one bit**: `n ^ (n >> 1)` gives 0, 1, 3, 2, 6, 7, 5, 4 for n = 0..7. Used in rotary encoders so that a half-way reading is never off by more than one step.

---

## 8. Byte order (endianness)

A 32-bit value occupies 4 bytes. Which byte goes at the lowest address?

```text
value 0x12345678 stored at address 0x100:

address       0x100  0x101  0x102  0x103
big-endian      12     34     56     78      most significant byte first (how we write numbers)
little-endian   78     56     34     12      least significant byte first
```

- **x86-64 and ARM64 as run on phones, Macs and servers are little-endian.** ARM can be configured either way, and some older router and embedded chips (MIPS, PowerPC) are big-endian, so check your target rather than assume.
- **The Internet protocols use big-endian** ("network byte order"): IP, TCP, UDP and the Ethernet type field carry multi-byte fields most-significant-byte first. Not every protocol does: **802.11 (Wi-Fi) frame fields are little-endian on the air**. Read the spec for each protocol.

A value in a register is just a number; byte order matters only when the value goes **to or from memory or a wire as bytes**. Look at it from C:

```c
uint32_t v = 0x12345678;
unsigned char *p = (unsigned char *)&v;      // look at v one byte at a time
printf("%02X %02X %02X %02X\n", p[0], p[1], p[2], p[3]);    // little-endian machine prints 78 56 34 12
```

**Decode a big-endian field from a packet buffer without caring about the host's byte order** by building the number from bytes:

```c
uint32_t be32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}
```

The casts matter: `b[0] << 24` on a plain `int` would shift into the sign bit, which is undefined behaviour. The library functions `ntohl`/`htonl` (and `__builtin_bswap32`) do the same job. The most common mistake is casting a packet buffer to `struct header *` and reading fields directly: it is wrong on little-endian hosts (no byte swap) and may be unaligned (section 11).

---

## 9. Floating point (IEEE-754)

Integers cannot hold 0.1 or 3.14, or numbers like 6 × 10²³. Floating point is **scientific notation in binary**: a sign, an exponent and a fraction.

```text
-6.25  =  -1.1001 (binary) × 2²          (6.25 = 110.01 in binary = 1.1001 × 2²)
```

IEEE-754 packs three fields into the bits:

```text
float (32 bits):   | s |  exponent (8)  |        fraction (23)        |
double (64 bits):  | s |  exponent (11) |        fraction (52)        |

value = (-1)^s  ×  1.fraction (binary)  ×  2^(exponent - bias)        bias = 127 (float), 1023 (double)
```

The leading `1.` is not stored (the "hidden bit"), which buys one extra bit of precision. The exponent is stored with a **bias**, so it can represent negative powers without a sign bit.

### Encoding −6.25 by hand

```text
1. sign = 1 (negative)
2. 6.25 = 110.01b = 1.1001b × 2²   ->  exponent = 2 + 127 = 129 = 1000 0001
3. fraction = the bits after "1." = 1001, padded with zeros to 23 bits
4. 1 | 10000001 | 10010000000000000000000  =  1100 0000 1100 1000 0000 ...  =  0xC0C80000
```

### Decoding 0x40490FDB

```text
0100 0000 0100 1001 0000 1111 1101 1011
sign 0 | exponent 1000 0000 = 128  ->  2^(128-127) = 2 | fraction 100 1001 0000 1111 1101 1011  ->  1.5707964
value = 1.5707964 × 2 = 3.1415927     (that is pi as a float)
```

### Special values

| Exponent field | Fraction | Meaning |
|---|---|---|
| all 0 | 0 | zero (there is +0 and -0) |
| all 0 | not 0 | **subnormal** (tiny numbers with no hidden 1, filling the gap near zero) |
| all 1 | 0 | **infinity** (`1.0f/0.0f`): `0x7F800000` |
| all 1 | not 0 | **NaN**, "not a number" (`0.0/0.0`): e.g. `0x7FC00000` |
| otherwise | any | a normal number |

NaN is **not equal to anything, not even itself** (`nan != nan` is true). That is also the standard test for it.

### The formats

| Format | Bits | Sign / exponent / fraction | Largest | Decimal digits |
|---|---|---|---|---|
| FP16 (half) | 16 | 1 / 5 / 10 | 65,504 | about 3 |
| BF16 (bfloat16) | 16 | 1 / 8 / 7 | about 3.4 × 10³⁸ | about 2 to 3 |
| FP32 (`float`) | 32 | 1 / 8 / 23 | about 3.4 × 10³⁸ | about 7 |
| FP64 (`double`) | 64 | 1 / 11 / 52 | about 1.8 × 10³⁰⁸ | about 15 to 16 |

BF16 is simply the top 16 bits of an FP32. Machine-learning hardware uses FP16 and BF16 to halve memory traffic: BF16 keeps FP32's range but little precision; FP16 has more precision but a small range.

### Why floating point surprises people

1. **Most decimal fractions cannot be stored exactly.** 0.1 in binary is 1.100110011001… × 2⁻⁴, repeating forever, so it must be rounded.

   ```text
   0.1f = 0x3DCCCCCD = 0.100000001490116...          (the double 0.1 is 0.1000000000000000055...)
   0.1 + 0.2 = 0.30000000000000004  in double, so  0.1 + 0.2 == 0.3  is false
   ```

   **Never compare floats with `==`.** Compare with a tolerance, e.g. `fabs(a - b) <= eps * max(fabs(a), fabs(b))`. Use integers (cents) for money.

2. **Rounding.** The default mode is **round to nearest, ties to even**. The other modes (toward zero, toward +infinity, toward −infinity) are for interval arithmetic and special code. Every operation is rounded, so errors accumulate: adding many small numbers to a big one loses them.

3. **Limited precision for integers.** A `float` has 24 significant bits, so integers are exact only up to 2²⁴ = 16,777,216. Above that, `16777216.0f + 1.0f` is still `16777216` (the exact result is a tie, rounded to even).

4. **Spacing grows with size.** Near 1.0 neighbouring floats are about 1.2 × 10⁻⁷ apart (`FLT_EPSILON`); near 1 billion they are 64 apart. For `double`, `DBL_EPSILON` is about 2.2 × 10⁻¹⁶.

5. **Addition is not associative.** `(a + b) + c` can differ from `a + (b + c)`: in a `double`, `(1e16 + 1.0) - 1e16` is `0.0`, but `(1e16 - 1e16) + 1.0` is `1.0`. So anything that changes the order of a sum (a vectorised loop, a multithreaded reduction, a GPU kernel, the `-ffast-math` flag) can change the last digits of the answer. Tutorial 21 comes back to this.

6. **Bonus fact:** for positive floats, comparing the bit patterns as integers gives the same order as comparing the numbers (1.0f is `0x3F800000`, 2.0f is `0x40000000`). Radix sorts and some lock-free code use this.

Floating-point *hardware* (how an adder or multiplier is built, and what rounding costs) is [tutorial 05](05_computer_arithmetic_and_the_alu.md) section 5.

---

## 10. Text: ASCII and UTF-8

**ASCII** assigns numbers 0 to 127 to characters. It fits in 7 bits (one byte with the top bit 0). A few to know:

```text
'0' = 0x30 (48)      'A' = 0x41 (65)      'a' = 0x61 (97)      '\n' = 0x0A      '\0' = 0x00
```

Digits are consecutive, so `c - '0'` converts a digit character to its value. Upper and lower case differ in one bit (0x20): `'A' | 0x20 == 'a'`.

ASCII cannot represent é, 日 or 😀. **Unicode** gives every character a number called a **code point** (written U+00E9 for é, U+20AC for €). **UTF-8** is the encoding that turns code points into bytes:

| Code point range | Bytes | Pattern |
|---|---|---|
| U+0000 to U+007F | 1 | `0xxxxxxx` (same as ASCII) |
| U+0080 to U+07FF | 2 | `110xxxxx 10xxxxxx` |
| U+0800 to U+FFFF | 3 | `1110xxxx 10xxxxxx 10xxxxxx` |
| U+10000 to U+10FFFF | 4 | `11110xxx 10xxxxxx 10xxxxxx 10xxxxxx` |

```text
é  U+00E9  = 1110 1001  -> 2 bytes:  110 00011  10 101001  = C3 A9
€  U+20AC  = 0010 0000 1010 1100 -> 3 bytes: 1110 0010  10 000010  10 101100 = E2 82 AC
😀 U+1F600                        -> 4 bytes: F0 9F 98 80
```

Properties worth knowing: UTF-8 is **backward compatible with ASCII**; the first byte tells you the length; continuation bytes always start with `10`, so you can find character boundaries from any position; and it has no byte-order problem. The consequence for C: **`strlen` counts bytes, not characters**. `strlen("€")` is 3.

Other encodings you will meet: **UTF-16** (Windows APIs, Java, JavaScript) uses 2-byte units, and code points above U+FFFF take a pair of units called a **surrogate pair**, so there too the number of units is not the number of characters. Linux, the web and network protocols use UTF-8.

---

## 11. Alignment and struct padding

A value of size *s* is **naturally aligned** if its address is a multiple of *s*: a 4-byte `int` at 0x1000 or 0x1004, not 0x1002. CPUs read memory in aligned chunks (and caches in 64-byte lines), so an unaligned access can be slower, can span two cache lines, can fault on some CPUs or for some instructions, and is not guaranteed atomic. So the compiler places each field at an address that suits it and inserts **padding**.

Layout rules (typical on x86-64 and ARM64):

1. Each field starts at a multiple of its own alignment.
2. The struct's size is rounded up to a multiple of its **largest field alignment**, so that arrays of the struct stay aligned.

```c
struct S1 { char a; int b; char c; };      // sizeof = 12
```

```text
offset: 0   1 2 3   4 5 6 7   8   9 10 11
        a   pad     b         c   pad          (b must start at a multiple of 4; end padded to a multiple of 4)
```

**Reorder fields from largest to smallest** and the padding disappears:

```c
struct S2 { int b; char a; char c; };      // sizeof = 8   (4 + 1 + 1 + 2 padding at the end)
struct S3 { char a; double d; char c; };   // sizeof = 24  (a, 7 padding, d, c, 7 padding)
struct S4 { double d; char a; char c; };   // sizeof = 16
```

Sizes of the basic types on 64-bit Linux and macOS (the "LP64" model; Windows has a 4-byte `long`):

| Type | Bytes | Type | Bytes |
|---|---|---|---|
| `char` | 1 | `float` | 4 |
| `short` | 2 | `double` | 8 |
| `int` | 4 | pointer | 8 |
| `long`, `long long` | 8 | `uint32_t`, `int64_t`... | exactly as named |

Use the fixed-width types from `<stdint.h>` whenever the size matters (file formats, packets, registers). **Bit-fields** (`unsigned flags : 3;`) pack small fields into one word, but their bit order and layout are implementation-defined, so avoid them for hardware registers and wire formats; the C track (tutorial 04) covers them.

Tools: `sizeof(T)`, `offsetof(T, field)`, `alignof(T)` (C++ / C11). `__attribute__((packed))` removes padding, but then fields can be misaligned: reading them may be slow or fault on some CPUs, and a pointer to such a field can be misaligned (GCC warns about it). For packets, prefer reading with `memcpy` into a local variable, or building the value byte by byte as in section 8.

---

## 12. Mistakes that cost points

1. **Mixing signed and unsigned** in a comparison (`-1 < 0u` is false) or a loop counter (`unsigned i; i >= 0`).
2. **Assuming signed overflow wraps.** It is undefined behaviour in C and C++; use unsigned arithmetic or check before the operation.
3. **`abs(INT_MIN)`** and `-INT_MIN`: there is no positive 2³¹ in a 32-bit `int`.
4. **Right-shifting a negative number and expecting division**: `-7 >> 1` is -4, `-7 / 2` is -3.
5. **Shifting by the type's width or more**, or shifting a 1 into the sign bit of an `int`.
6. **Comparing floats with `==`**, or using a `float` loop counter or accumulator that exceeds 2²⁴.
7. **Reading multi-byte fields from a packet with a pointer cast**: wrong byte order and possibly misaligned.
8. **Treating `strlen` as the number of characters** in UTF-8 text.
9. **Forgetting that plain `char` may be signed** (x86) or unsigned (ARM Linux): `char c = 0xB4;` is -76 on one and 180 on the other.
10. **Guessing struct sizes.** Print `sizeof` and `offsetof`.

---

## 13. Questions you will be asked

**What is -1 as a 32-bit value in hex? What does `(unsigned)-1 > 0` give?** `0xFFFFFFFF`, because flipping all bits of 1 and adding 1 gives all ones. The comparison is true: `(unsigned)-1` is 4,294,967,295.

**Why do computers use two's complement?** One zero, and the same adder works for signed and unsigned numbers, so subtraction needs no extra hardware; negation is flip-and-add-one.

**What is the difference between carry and overflow?** Carry is unsigned overflow (a carry out of the top bit). Overflow is signed overflow (two numbers of the same sign gave the opposite sign). The CPU sets both; software looks at the one that matches the type.

**Is signed integer overflow defined in C?** No, it is undefined behaviour; unsigned overflow wraps. The compiler may use that to optimise (for example, assuming `x + 1 > x`).

**What is endianness, and when does it matter?** The order of a value's bytes in memory. It matters when values cross a boundary as bytes: network packets, files, shared memory between machines. x86-64 and ARM64 are little-endian; network byte order is big-endian.

**Why is `0.1 + 0.2 != 0.3`?** 0.1 and 0.2 are not exactly representable in binary, so each is rounded; their sum is rounded again and lands on a different double than 0.3 does. Compare with a tolerance.

**How many bits does a `float` have for precision, and what follows?** 24 (23 stored plus the hidden bit), about 7 decimal digits. Integers above 2²⁴ cannot all be represented.

**What is the size of `struct { char a; int b; char c; }`, and how would you shrink it?** 12 bytes on a typical 64-bit system; put `int b` first and it becomes 8.

**What is the difference between a code point and a byte in UTF-8?** A code point is a character's number (U+20AC); UTF-8 encodes it in 1 to 4 bytes (€ takes 3).

---

## 14. Problems

**How to use this section.** Try each problem before reading the answer at the bottom. A and B are on paper (about 20 minutes each), C on paper (20 minutes), D needs a C compiler (30 minutes). For each, write down your answer before checking.

### Problem A (paper): bases and two's complement

(a) Write 217 in binary and in hex.
(b) What does the 8-bit pattern `0xB4` mean as an unsigned number? As a signed number?
(c) Write -45 as an 8-bit two's-complement number, in binary and hex.
(d) Sign-extend `0xB4` from 8 to 16 bits. Zero-extend it too.
(e) What range of values can a 12-bit signed integer hold?

*Where:* paper. *How:* group binary digits in fours for hex. For a negative number, write the positive value first, flip all bits, add 1. To read a pattern as signed, either use the MSB weight of -128 or subtract 256 if the MSB is set.

### Problem B (paper): flags and shifts

(a) For each 8-bit addition, give the result and say whether **CF** (carry) and **OF** (overflow) are set: `0x7F + 0x01`, `0xF0 + 0x20`, `0xC8 + 0x64`, `0x80 + 0xFF`.
(b) Evaluate: `0xF0 >> 4` as an unsigned 8-bit value; `-20 >> 2` as a signed 8-bit value; `-9 >> 1`; `-9 / 2`.
(c) A colleague writes `if (x + 1 > x)` to detect "not the largest int". What goes wrong?

*Where:* paper. *How:* do the addition in binary, keep 8 bits. CF is a carry out of bit 7. OF is set when both operands have the same sign bit and the result's sign bit differs. For (b), an arithmetic shift copies the sign bit into the top.

### Problem C (paper): floats

(a) Decode `0xC1480000` as a `float`.
(b) Encode 10.75 as a `float` and write it in hex.
(c) `float f = 16777216.0f; f = f + 1.0f;` What is `f` afterwards, and why?
(d) Why does the sum of ten 0.1 values in a `double` not exactly equal 1.0 (try it in code if you like)?

*Where:* paper (and a few lines of C for (d)). *How:* split the 32 bits into 1 / 8 / 23; the exponent is the middle 8 bits minus 127; the value is ±1.fraction × 2^exponent. To encode, write the number in binary, normalise it to 1.xxx × 2ᵏ, then add the bias 127 to k.

### Problem D (hands-on C): predict, then check

Write a small C program (one file, compile with `gcc -Wall -Wextra`) that prints:

1. `sizeof` for `struct { char a; int b; char c; }`, the same fields reordered as `int b; char a; char c;`, and `struct { char a; double d; char c; }`. **Predict all three first.**
2. The four bytes of `uint32_t v = 0x12345678` in memory order. What does your machine print?
3. The bit pattern of `0.1f` and of `-6.25f` in hex (copy the float into a `uint32_t` with `memcpy`).
4. `strlen("€")` for a source file saved in UTF-8.

*Where:* your editor and terminal (Mac or Linux). *How:* the pieces you need are `sizeof`, `memcpy`, `printf("%08X")` and a pointer to `unsigned char` for the bytes. Write your predictions as comments before running, then compare.

---

## Answers

Try before reading.

### Problem A

(a) 217 = 128 + 64 + 16 + 8 + 1 = `1101 1001` = **0xD9**.

(b) `0xB4` = `1011 0100`. Unsigned: 11×16 + 4 = **180**. Signed: -128 + 32 + 16 + 4 = **-76** (also 180 − 256).

(c) 45 = `0010 1101`; flip: `1101 0010`; add 1: `1101 0011` = **0xD3**. Check: -128 + 64 + 16 + 2 + 1 = -45.

(d) Sign-extended: **0xFFB4** (still -76). Zero-extended: **0x00B4** (still 180).

(e) -2¹¹ to 2¹¹ − 1 = **-2048 to +2047**.

### Problem B

(a)

| Addition | Result | CF | OF | Why |
|---|---|---|---|---|
| `0x7F + 0x01` | `0x80` | 0 | **1** | 127+1 = 128 does not fit signed; no carry out |
| `0xF0 + 0x20` | `0x10` | **1** | 0 | carry out of bit 7; -16 + 32 = 16 fits |
| `0xC8 + 0x64` | `0x2C` | **1** | 0 | 200+100 = 300 needs 9 bits; -56 + 100 = 44 fits |
| `0x80 + 0xFF` | `0x7F` | **1** | **1** | -128 + -1 = -129 does not fit; carry out too |

(b) `0xF0 >> 4` (unsigned, logical) = **`0x0F`**. `-20 >> 2` = **-5**. `-9 >> 1` = **-5** (floor of -4.5), while `-9 / 2` = **-4** (truncation toward zero).

(c) Signed overflow is undefined behaviour. For `x == INT_MAX`, `x + 1` overflows, and the compiler is allowed to assume it never does, so it may optimise the whole condition to `true` (it often does at `-O2`), and the check never fires. The safe version is `if (x < INT_MAX)`.

### Problem C

(a) `0xC1480000` = `1 10000010 10010000000000000000000`. Sign 1 (negative). Exponent 130 − 127 = 3. Fraction `1001` gives 1.5625. Value = −1.5625 × 2³ = **-12.5**.

(b) 10.75 = `1010.11` = 1.01011 × 2³. Exponent 3 + 127 = 130 = `10000010`. Fraction `01011000...`. Bits: `0 10000010 01011000000000000000000` = `0100 0001 0010 1100 0000 ...` = **0x412C0000**.

(c) `f` stays **16777216.0**. A float has 24 significant bits; at 2²⁴ neighbouring floats are 2 apart, so 16777217 is not representable. The exact sum is half-way between 16777216 and 16777218, and ties round to the even mantissa, which is 16777216.

(d) 0.1 is stored as 0.1000000000000000055…, a bit too large. Adding it ten times accumulates rounding errors, and the sum comes out as `0.99999999999999989` (just below 1.0). The values are close but not equal, so compare with a tolerance.

### Problem D

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>

struct A { char a; int b; char c; };       // predict 12
struct B { int b; char a; char c; };       // predict 8
struct C { char a; double d; char c; };    // predict 24

int main(void) {
    printf("%zu %zu %zu\n", sizeof(struct A), sizeof(struct B), sizeof(struct C));

    uint32_t v = 0x12345678;
    unsigned char *p = (unsigned char *)&v;
    printf("%02X %02X %02X %02X\n", p[0], p[1], p[2], p[3]);

    float f1 = 0.1f, f2 = -6.25f;
    uint32_t b1, b2;
    memcpy(&b1, &f1, 4);
    memcpy(&b2, &f2, 4);
    printf("%08X %08X\n", b1, b2);

    printf("%zu\n", strlen("€"));
    return 0;
}
```

Expected output on a typical x86-64 or Apple-silicon Mac or Linux machine:

```text
12 8 24
78 56 34 12
3DCCCCCD C0C80000
3
```

- Sizes: `char a` at 0, 3 bytes padding, `int b` at 4, `char c` at 8, 3 bytes of tail padding, giving 12. Reordered, `int` at 0, then two chars, then 2 padding bytes, giving 8. With a `double` field alignment is 8, so `char` + 7 padding + `double` + `char` + 7 padding = 24.
- `78 56 34 12` means little-endian. A big-endian machine would print `12 34 56 78`.
- `0.1f` is `3DCCCCCD` (exponent 123, fraction rounded up from the repeating `1001`), and `-6.25f` is `C0C80000`, matching the hand encoding in section 9.
- `strlen("€")` is 3 because UTF-8 (the default for source files on Mac and Linux) stores € as `E2 82 AC`, and `strlen` counts bytes, never characters.

---

## Cheat sheet

- *n* bits give 2ⁿ patterns. One hex digit is 4 bits. KiB is 2¹⁰, 4 KiB pages are 2¹².
- Bits mean nothing until a **type** is chosen.
- Unsigned: 0 to 2ⁿ−1, wraps modulo 2ⁿ (defined in C).
- Signed (two's complement): MSB weighs −2ⁿ⁻¹; negate = flip bits + 1; range −2ⁿ⁻¹ to 2ⁿ⁻¹−1 (lopsided); **signed overflow is undefined in C**.
- Sign-extend signed values, zero-extend unsigned ones; narrowing drops the high bits.
- Mixed signed/unsigned comparison: the signed value becomes unsigned (`-1 < 0u` is false).
- **CF** = unsigned overflow. **OF** = same-sign operands gave an opposite-sign result.
- `>>` on signed values rounds toward minus infinity; `/` rounds toward zero. Shift counts must be less than the width.
- `x & (x-1)` clears the lowest set bit; `x & -x` isolates it.
- Little-endian: x86-64, ARM64 (in practice). Internet protocols: big-endian; 802.11 frames: little-endian. Build integers from bytes; do not cast packet pointers.
- Float = sign / exponent / fraction, bias 127 (float) or 1023 (double), hidden leading 1. Specials: exponent all 0 (zero, subnormal), all 1 (infinity, NaN). 24 bits of precision: integers exact to 2²⁴. Never compare with `==`.
- ASCII is 7 bits; UTF-8 is 1 to 4 bytes per code point and `strlen` counts bytes.
- Fields align to their own size; the struct pads to its largest field alignment. Order fields large to small.

**Next:** tutorial 03, Boolean Logic and Combinational Circuits (gates, K-maps, adders; a Skim tutorial). Say "next" when you are ready, or tell me to jump to a Core one such as tutorial 06.
