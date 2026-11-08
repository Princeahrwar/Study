# DSA 11.01: Bit Manipulation

*Part of the [Bits and math plan](00_plan.md) · Time: about 5 hours (2.5 reading, 2.5 problems) · You need: [C 01: types, conversions and undefined behaviour](../../02_c_language/01_types_conversions_and_undefined_behavior.md) (unsigned types, shifts, why `1 << 31` is a trap), and arrays from [Arrays and strings](../01_arrays_and_strings/00_plan.md)*

Bit tricks look like magic until you see that nearly all of them are one of three moves: **isolate the lowest set bit**, **clear the lowest set bit**, or **cancel pairs with XOR**. This tutorial builds those moves from the arithmetic that makes them work, then uses them for the problems that come up in interviews: counting bits, finding the one number that is different, enumerating subsets, and doing arithmetic with no `+`.

By the end you can:

- write each single-bit operation and the three identities from memory, and say **why** `x & (x - 1)` clears the lowest set bit;
- count set bits four ways and say which one to use when;
- solve the "everything appears twice except one" family (one, two, or three-times variants) and the "missing number" family with XOR;
- treat an integer as a **set** of up to 64 items: iterate all subsets, all submasks of a mask, all masks with exactly `k` bits, and sum over subsets;
- do addition, division and multiplication with shifts, find the AND of a range, and the maximum XOR of two numbers;
- spot the undefined-behaviour traps (shifts by the width, signed shifts) that the tools catch for you.

**Where the numbers come from.** The code below was compiled with `-Wall -Wextra` and tested: 5,282,884 checks passed under AddressSanitizer and UBSan (GCC 13.3). Every function is compared with a plain brute force: bit tests against division and remainder, the four popcounts against each other and a 32-step loop, reverse against a bit-by-bit build, the XOR puzzles against shuffled inputs with known answers, submask and `k`-subset enumeration against a loop over all masks, sum over subsets against the `O(4^n)` definition, and `add_no_plus`, `divide_shift`, `mul_peasant`, `range_and` and `max_xor_pair` against the real operators. The tests found one real bug while I wrote this (section 8), which is why the tools in C 01 matter.

**Data and timing.** Inputs are synthetic and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are the **best of 5 repetitions, shown for run 1 / run 2**, over 4,194,304 numbers; they are this machine's, not yours.

---

## 1. Bits, operators and the rules that keep you out of trouble

An unsigned 32-bit integer is 32 bits, numbered 0 (least significant) to 31. The operators:

| Operator | Meaning | Example (8 bits) |
|---|---|---|
| `a & b` | 1 where both are 1 | `1100 & 1010 = 1000` |
| `a \| b` | 1 where either is 1 | `1100 \| 1010 = 1110` |
| `a ^ b` | 1 where they differ | `1100 ^ 1010 = 0110` |
| `~a` | flip every bit | `~00001100 = 11110011` |
| `a << k` | move left `k` places, fill with 0 (multiply by `2^k`) | `0011 << 2 = 1100` |
| `a >> k` | move right `k` places (divide by `2^k`, rounding down for unsigned) | `1100 >> 2 = 0011` |

Rules for writing it in C and C++ (the reasons are in C 01):

1. **Use unsigned types** (`uint32_t`, `uint64_t`, `unsigned`) for bit work. Right shift of a negative `int` is implementation-defined, and `1 << 31` on an `int` is undefined.
2. **Write masks as `1u << k` or `1ull << k`**, never `1 << k`.
3. **Keep the shift count in `0 .. width-1`.** A shift by 32 of a 32-bit value is undefined, not zero.
4. Small types are promoted to `int` before any operator, so `~uint8_t` and `uint8_t << 8` are computed on an `int`.
5. Put parentheses around bit operators: `&`, `^`, `|` bind **looser than `==`**, so `x & 1 == 0` means `x & (1 == 0)`. GCC warns; read the warning.

The four single-bit operations:

```cpp
// Single-bit operations.  Always use an UNSIGNED type and 1u / 1ull, never a plain int 1 (see C 01: 1 << 31 on an int is undefined).
inline bool test_bit (u32 x, int k) { return (x >> k) & 1u; }
inline u32  set_bit  (u32 x, int k) { return x |  (1u << k); }
inline u32  clear_bit(u32 x, int k) { return x & ~(1u << k); }
inline u32  flip_bit (u32 x, int k) { return x ^  (1u << k); }
```

`x >> k & 1` reads bit `k`; `x | (1u << k)` sets it; `x & ~(1u << k)` clears it; `x ^ (1u << k)` flips it. To work on a **range**, build the mask with `((1u << len) - 1) << start` (careful when `len` is 32).

## 2. The three identities

Start from one fact about arithmetic: **subtracting 1 from a binary number borrows from the lowest 1 bit**. That bit becomes 0 and every 0 below it becomes 1. Everything above is unchanged. Adding 1 does the mirror image: the lowest 0 becomes 1 and the 1s below it become 0.

```
x          = 1 0 1 0 1 1 0 0
x - 1      = 1 0 1 0 1 0 1 1      the lowest 1 (bit 2) became 0, the zeros below it became 1
```

From that:

- **`x & (x - 1)` clears the lowest set bit.** Above the borrow point `x` and `x - 1` agree, so those bits survive. At and below it they are complements, so the AND clears them.
- **`x & -x` isolates the lowest set bit.** In two's complement `-x` is `~x + 1`, which equals `~(x - 1)`. Above the borrow point `-x` is the complement of `x`, so the AND clears those bits. At the borrow point both have 1. Below it both have 0. Only one bit remains.
- **`x | (x + 1)` sets the lowest clear bit.**

The measured trace for `x = 10101100`:

```
T1 x=10101100
T1 x-1=10101011 x&(x-1)=10101000 x&-x=00000100 x|(x+1)=10101101
```

Read it: `x - 1` is `10101011`; `x & (x - 1)` is `10101000` (the lowest set bit, bit 2, is gone); `x & -x` is `00000100` (only that bit remains); `x | (x + 1)` is `10101101` (the lowest 0, bit 0, is now 1).

```cpp
// The three identities that solve half of all bit problems.  Each works because adding or subtracting 1 flips a run of low bits.
inline u32 clear_lowest_set(u32 x) { return x & (x - 1); }   // 101100 -> 101000   (x - 1 turns the lowest 1 into 0 and the zeros below it into 1s)
inline u32 isolate_lowest_set(u32 x) { return x & (~x + 1); } // 101100 -> 000100   (same as x & -x, written without unary minus on unsigned)
inline u32 set_lowest_clear(u32 x) { return x | (x + 1); }   // 101011 -> 101111   (x + 1 turns the lowest 0 into 1)
inline bool is_pow2(u32 x) { return x != 0 && (x & (x - 1)) == 0; }
inline bool is_pow4(u32 x) { return is_pow2(x) && (x & 0x55555555u) != 0; }
```

Two uses to remember:

- **Power of two:** a positive number is a power of two exactly when it has one set bit, so `x != 0 && (x & (x - 1)) == 0`. The `x != 0` matters: `0 & -1` is also 0. A **power of four** is a power of two whose set bit is at an even position, so also `x & 0x55555555 != 0`.
- **Iterate set bits:** `for (; x; x &= x - 1)` visits each set bit once, and `x & -x` hands you that bit as a value. This is the idiom behind submask enumeration and Fenwick trees (the `i += i & -i` step).

## 3. Counting set bits

The **population count** (popcount) of `x` is the number of 1 bits. Four ways, in the order you would think of them:

```cpp
// Counting set bits, four ways.
inline int popcount_loop(u32 x) { int c = 0; for (; x; x >>= 1) c += x & 1u; return c; }          // 32 iterations always
inline int popcount_kernighan(u32 x) { int c = 0; for (; x; x &= x - 1) ++c; return c; }           // one iteration per set bit
inline int popcount_swar(u32 x) {                                                                  // parallel adds in 5 steps, no loop
    x = x - ((x >> 1) & 0x55555555u);                       // each 2-bit field now holds the count of its 2 bits
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);       // each 4-bit field holds a count 0..4
    x = (x + (x >> 4)) & 0x0F0F0F0Fu;                       // each byte holds a count 0..8
    return (int)((x * 0x01010101u) >> 24);                  // the multiply sums the four bytes into the top byte
}
inline int popcount_builtin(u32 x) { return __builtin_popcount(x); }
```

- `popcount_loop` shifts and tests every position: always 32 iterations.
- `popcount_kernighan` removes one set bit per iteration with `x &= x - 1`: as many iterations as there are set bits.
- `popcount_swar` ("SIMD within a register") adds neighbouring bit fields in parallel: first pairs of bits, then nibbles, then bytes, and one multiplication sums the four bytes into the top byte. Five steps and no loop or branch.
- `__builtin_popcount` lets the compiler pick; with `-mpopcnt` (or on ARM64, which always has it) it becomes one instruction. In C++20 the portable name is `std::popcount` from `<bit>`.

The SWAR steps on `0xF0F0F0F1` (17 set bits) give the same answer as the loop, which the test also checks on every random input:

```
T2 swar x=0xF0F0F0F1 -> 17 (loop 17)
```

Timing over 4,194,304 numbers (milliseconds, run 1 / run 2):

| Method | Random 32-bit numbers (about 16 set bits) | Sparse numbers (2 set bits) |
|---|---|---|
| `popcount_loop` | 78.59 / 66.39 | 73.60 / 71.55 |
| `popcount_kernighan` | 73.23 / 70.09 | 5.69 / 5.07 |
| `popcount_swar` | 3.48 / 3.64 | 3.41 / 3.29 |
| `__builtin_popcount`, no `-mpopcnt` | 6.87 / 7.00 | not measured |

With `-mpopcnt` (one run each) the SWAR and builtin rows measured 2.36 / 2.06 ms, and the Kernighan row fell to 2.91 ms because GCC recognised the idiom and emitted the popcount instruction (the assembly shows `popcnt`); the plain loop stayed at 75.09 ms. Three readings:

- Kernighan is **no better than the plain loop on dense inputs** and about 13 times faster on sparse ones (the loop pays 32 steps either way). Choose it only when you expect few set bits.
- SWAR is about 20 times faster than both here. I did not inspect the generated code, so the compiler may have vectorised the loop; treat the ratio as "much faster", not as a constant.
- Without `-mpopcnt`, GCC calls a library routine for the builtin and it is slower than the SWAR above. In an interview, say "I would use the builtin or `std::popcount`", and know how to write Kernighan on a whiteboard.

## 4. Where is the bit? Logs, powers of two, reversal, parity

```cpp
inline int ctz32(u32 x) { return __builtin_ctz(x); }   // trailing zeros; undefined for x == 0
inline int clz32(u32 x) { return __builtin_clz(x); }   // leading zeros;  undefined for x == 0
inline int floor_log2(u32 x) { return 31 - __builtin_clz(x); }   // x > 0
inline u32 floor_pow2(u32 x) { return x ? 1u << floor_log2(x) : 0; }   // largest power of two <= x
inline u32 ceil_pow2(u32 x) { return x <= 1 ? 1 : 1u << (32 - __builtin_clz(x - 1)); }   // smallest power of two >= x (x <= 2^31)

inline u32 reverse_bits(u32 x) {              // swap halves, then quarters, ... : 5 steps
    x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
    x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
    x = ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);
    x = ((x >> 8) & 0x00FF00FFu) | ((x & 0x00FF00FFu) << 8);
    return (x >> 16) | (x << 16);
}
inline int parity(u32 x) { x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1; return x & 1u; }
```

- `ctz` (count trailing zeros) is the **index of the lowest set bit**; `clz` (count leading zeros) gives the **index of the highest**: `31 - clz(x)` is `floor(log2 x)`. Both are undefined for `x == 0` (the hardware instruction has no answer), so test for zero first.
- `ceil_pow2` rounds up: for `x > 1`, the smallest power of two `>= x` has bit index `32 - clz(x - 1)`. The case `x <= 1` is handled separately; for `x > 2^31` the answer does not fit in 32 bits.
- `reverse_bits` swaps adjacent bits, then adjacent pairs, then nibbles, bytes and halves: five steps, the same shape as SWAR popcount. LeetCode 190 is exactly this.
- `parity` folds the word onto itself with XOR; the last bit is 1 when the popcount is odd.

## 5. XOR puzzles

XOR has three properties that do all the work: `a ^ a = 0`, `a ^ 0 = a`, and it is commutative and associative. XOR a pile of numbers together and **every value that appears twice cancels**.

```cpp
// XOR: a ^ a == 0, a ^ 0 == a, commutative and associative.  A pile of numbers XORed together cancels every pair.
int single_number(const std::vector<int>& a) {            // every value twice except one
    int r = 0; for (int v : a) r ^= v; return r;
}
int single_number_thrice(const std::vector<int>& a) {     // every value three times except one: count each bit position mod 3
    int r = 0;
    for (int b = 0; b < 32; ++b) {
        int c = 0; for (int v : a) c += ((unsigned)v >> b) & 1u;
        if (c % 3) r |= (int)(1u << b);
    }
    return r;
}
std::pair<int,int> single_numbers_two(const std::vector<int>& a) {   // every value twice except two different ones, x and y
    unsigned all = 0; for (int v : a) all ^= (unsigned)v;           // all == x ^ y, nonzero
    unsigned low = all & (~all + 1);                                // a bit where x and y differ
    int x = 0, y = 0;
    for (int v : a) ((unsigned)v & low ? x : y) ^= v;               // split into two groups; each group has one unpaired value
    if (x > y) std::swap(x, y);
    return {x, y};
}
int missing_number(const std::vector<int>& a) {           // a holds 0..n except one value
    int r = (int)a.size();
    for (int i = 0; i < (int)a.size(); ++i) r ^= i ^ a[i];
    return r;
}
u32 xor_upto(u32 n) {                                     // 0 ^ 1 ^ ... ^ n in O(1): the answer repeats with period 4
    switch (n & 3u) { case 0: return n; case 1: return 1; case 2: return n + 1; default: return 0; }
}
u32 gray(u32 i) { return i ^ (i >> 1); }                  // consecutive Gray codes differ in exactly one bit
u32 gray_inverse(u32 g) { for (u32 s = g >> 1; s; s >>= 1) g ^= s; return g; }
```

- **`single_number` (LeetCode 136):** every value appears twice but one. The XOR of everything is that value. `O(n)` time, `O(1)` space; a hash set would need `O(n)` space.
- **`single_number_thrice` (LeetCode 137):** every value three times but one. XOR no longer cancels triples, but **counting each bit position modulo 3** does: the set positions of the answer are the positions where the count is not a multiple of 3. This generalises to "every value `k` times": count modulo `k`.
- **`single_numbers_two` (LeetCode 260):** two values `x` and `y` appear once, the others twice. XOR of all is `x ^ y`, nonzero since `x != y`. Any set bit of `x ^ y` is a position where `x` and `y` differ, so splitting the numbers by that bit puts `x` and `y` in different groups, and each group is then a "single number" problem. We take the lowest such bit with `all & -all`. The trace on `{1, 2, 1, 3, 2, 5}`:

```
T8 single_numbers_two({1,2,1,3,2,5}) = 3 5
```

- **`missing_number` (LeetCode 268):** `a` holds `0..n` with one missing. XOR the indices `0..n` with the values: everything present cancels. (The sum formula `n(n+1)/2 - sum` works too; XOR cannot overflow.)
- **`xor_upto`:** `0 ^ 1 ^ ... ^ n` repeats with period 4, so it is `O(1)`:

```
T7 xor_upto(0..8): 0 1 3 0 4 1 7 0 8
```

  The values are `n, 1, n + 1, 0` for `n mod 4 = 0, 1, 2, 3`. XOR of a range `[l, r]` is `xor_upto(r) ^ xor_upto(l - 1)`.
- **Swap without a temporary:** `a ^= b; b ^= a; a ^= b;` swaps correctly when `a` and `b` are different objects. If they alias the same variable it zeroes it. Never use it for real code; it is slower on modern CPUs and an interview trap.
- **Gray code:** `i ^ (i >> 1)` is a sequence where consecutive values differ in one bit. The first eight values for 3 bits:

```
T3 gray: 000 001 011 010 110 111 101 100
```

  The inverse folds the shifts back with XOR (`gray_inverse`). Gray codes number the squares of a Karnaugh map, drive rotary encoders, and solve LeetCode 89.

## 6. Counting bits over many numbers

**Bits of every number from 0 to `n` (LeetCode 338).** `bits[i] = bits[i >> 1] + (i & 1)`: the number without its last bit was already counted. `O(n)` total, instead of `O(n log n)`.

**Sum of Hamming distances over all pairs (LeetCode 477).** The Hamming distance of two numbers is `popcount(a ^ b)`. For all `n(n-1)/2` pairs, do **each bit position separately**: if `c` of the numbers have that bit set, exactly `c * (n - c)` pairs differ there. This is the general trick for "sum over pairs of a per-bit quantity": the bit positions are independent, so you count per position and add.

```cpp
// Counting bits for every number 0..n at once.  bits[i] = bits[i >> 1] + (i & 1): i without its last bit was already counted.
std::vector<int> count_bits_upto(int n) {
    std::vector<int> b(n + 1, 0);
    for (int i = 1; i <= n; ++i) b[i] = b[i >> 1] + (i & 1);
    return b;
}
int hamming_distance(u32 a, u32 b) { return popcount_kernighan(a ^ b); }
// Sum of Hamming distances over ALL pairs: look at each bit position alone.  With c numbers having that bit set and n - c not,
// c * (n - c) pairs differ there.  O(32 n) instead of O(n^2).
long long total_hamming(const std::vector<u32>& a) {
    long long tot = 0; const long long n = (long long)a.size();
    for (int b = 0; b < 32; ++b) {
        long long c = 0; for (u32 v : a) c += (v >> b) & 1u;
        tot += c * (n - c);
    }
    return tot;
}
```

```
T11 bits 0..8: 0 1 1 2 1 2 2 3 1
T10 total_hamming({4,14,2})=6
```

For `{4, 14, 2}` (binary `0100`, `1110`, `0010`): bit 1 is set in 14 and 2, so `c = 2` and `2 * 1 = 2` pairs differ there; bit 2 is set in 4 and 14, again 2; bit 3 is set only in 14, so `1 * 2 = 2`. The total is 6, which matches the brute force over the three pairs.

## 7. Bitmasks as sets

A `u32` (or `u64`) with bit `i` set means "element `i` is in the set". It is the fastest way to represent a small set: union `|`, intersection `&`, difference `a & ~b`, membership `(m >> i) & 1`, size `popcount`, and **equality is integer equality**, so a mask can be a hash key or an array index (the DP states in [bitmask DP](../10_dynamic_programming/06_bitmask_and_state_machine_dp.md)).

```cpp
// A bitmask is a set of up to 64 small integers.  Union |, intersection &, difference & ~, membership (m >> i) & 1.
// All submasks of mask, in decreasing order, including mask itself and the empty set.  Total work over all masks of n bits: 3^n.
template <class F> void for_each_submask(u32 mask, F f) {
    u32 sub = mask;
    while (true) { f(sub); if (sub == 0) break; sub = (sub - 1) & mask; }
}
// All masks of n bits (n <= 31) with exactly k bits set, in increasing order (Gosper's hack).  The next larger number with the same
// popcount: move the lowest block of ones up by one and put the leftover ones back at the bottom.
template <class F> void for_each_k_subset(int n, int k, F f) {
    if (k == 0) { f(0u); return; }
    u32 x = (1u << k) - 1;
    while (x < (1u << n)) {
        f(x);
        u32 c = x & (~x + 1), r = x + c;
        x = (((r ^ x) >> 2) / c) | r;
    }
}
// Sum over subsets (SOS DP): out[m] = sum of in[s] over all submasks s of m.  n * 2^n instead of 3^n.
std::vector<long long> sum_over_subsets(std::vector<long long> f, int n) {
    for (int i = 0; i < n; ++i)
        for (u32 m = 0; m < (1u << n); ++m)
            if (m >> i & 1u) f[m] += f[m ^ (1u << i)];
    return f;
}
```

**All subsets of `n` items:** loop `m` from 0 to `2^n - 1`; each `m` is one subset. For `n = 20` that is about a million masks.

**All submasks of a given mask** (the subsets of a subset). Go from `mask` downward with `sub = (sub - 1) & mask`: subtracting 1 flips the low bits, and AND-ing with `mask` throws away the bits that are not allowed, landing on the next smaller submask. For `mask = 1011`:

```
T4 submasks of 1011: 1011 1010 1001 1000 0011 0010 0001 0000
```

Eight submasks, `2^3` because `1011` has three set bits. **Summed over every mask of `n` bits, the number of submask visits is exactly `3^n`**: each element is in the mask and the submask, in the mask only, or in neither. Measured:

```
T12 n=10 submask visits=59049 3^n=59049
T12 n=16 submask visits=43046721 3^n=43046721
T12 n=20 submask visits=3486784401 3^n=3486784401
```

`3^n` is the cost of "for every mask, for every submask", the loop inside partition-style bitmask DPs. It is fine for `n` up to about 16 to 18 and hopeless at 25.

**All masks with exactly `k` bits set** (Gosper's hack). The next larger number with the same popcount is built from the lowest block of ones: `c = x & -x` is its lowest bit, `r = x + c` carries that block up by one place (the block becomes a single 1 higher up), and `((r ^ x) >> 2) / c` is the leftover ones, pushed back to the bottom. For `n = 4`, `k = 2`:

```
T5 k-subsets n=4 k=2: 0011 0101 0110 1001 1010 1100
```

Six masks, `C(4, 2)`, in increasing order, with no wasted candidates. A loop over all `2^n` masks with a popcount test costs `2^n`; Gosper's costs `C(n, k)`.

**Sum over subsets (SOS DP).** Given `f[s]` for each `s`, compute `g[m]` = sum of `f[s]` over all submasks `s` of `m`, for every `m`. Doing the submask loop per mask costs `3^n`. SOS processes one bit at a time: after step `i`, `f[m]` holds the sum over submasks that differ from `m` only in bits `0..i`. Each step adds `f[m without bit i]` into `f[m]` when bit `i` is set. Total `n * 2^n`. The test compares it with the `O(4^n)` definition for `n` up to 9. Same trick, with `+` replaced by OR or max, gives "does any submask satisfy..." and the zeta transform used in subset-convolution problems. Mention it if asked for something faster than `3^n`.

## 8. Arithmetic out of bits

```cpp
// Arithmetic out of bits.
int add_no_plus(int a, int b) {                           // XOR is addition without carry; AND shifted left is the carry
    unsigned x = (unsigned)a, y = (unsigned)b;
    while (y) { unsigned carry = (x & y) << 1; x ^= y; y = carry; }
    return (int)x;                                        // conversion back to int is implementation-defined for big values; two's complement everywhere
}
// Divide a by b (truncating) using shifts and subtraction only, in 32-bit.  Handles INT_MIN / -1 by clamping, as LeetCode 29 asks.
int divide_shift(int dividend, int divisor) {
    if (dividend == INT_MIN && divisor == -1) return INT_MAX;
    bool neg = (dividend < 0) != (divisor < 0);
    unsigned long long a = dividend < 0 ? -(long long)dividend : dividend, b = divisor < 0 ? -(long long)divisor : divisor;
    unsigned long long q = 0;
    for (int s = 31; s >= 0; --s)
        if ((a >> s) >= b) { a -= b << s; q |= 1ull << s; }
    return neg ? (int)(0u - (unsigned)q) : (int)q;   // negate in unsigned arithmetic: q can be 2^31 (INT_MIN / 1)
}
u64 mul_peasant(u64 a, u64 b) {                           // multiply using shifts and adds, wrapping modulo 2^64: the Russian peasant method
    u64 r = 0; while (b) { if (b & 1) r += a; a <<= 1; b >>= 1; } return r;
}
// Bitwise AND of every integer in [lo, hi]: only the common binary prefix of lo and hi survives.
u32 range_and(u32 lo, u32 hi) {
    int s = 0; while (lo != hi) { lo >>= 1; hi >>= 1; ++s; }
    return s >= 32 ? 0 : lo << s;                         // s == 32 happens for [0, 2^32 - 1]; a shift by 32 would be undefined
}
// Maximum XOR of two numbers in a set: build the answer bit by bit from the top; a prefix candidate is reachable iff
// some pair of prefixes XORs to it (checked with a set of prefixes).
u32 max_xor_pair(const std::vector<u32>& a) {
    u32 best = 0, mask = 0;
    for (int b = 31; b >= 0; --b) {
        mask |= 1u << b;
        std::vector<u32> pre; pre.reserve(a.size());
        for (u32 v : a) pre.push_back(v & mask);
        std::sort(pre.begin(), pre.end()); pre.erase(std::unique(pre.begin(), pre.end()), pre.end());
        u32 cand = best | (1u << b); bool ok = false;
        for (u32 p : pre) if (std::binary_search(pre.begin(), pre.end(), p ^ cand)) { ok = true; break; }
        if (ok) best = cand;
    }
    return best;
}
```

- **Add without `+` (LeetCode 371).** XOR is addition **without carries**, and `(a & b) << 1` is exactly the carries. Add the two and repeat until the carry is zero. The trace for `13 + 11`:

```
T6 add_no_plus(13,11): [sum=6 carry=18] [sum=20 carry=4] [sum=16 carry=8] [sum=24 carry=0] => 24
```

  Each round the carry is pushed at least one position higher, so at most 32 rounds. It runs on `unsigned`, so the carry shifting out of the top is defined; with `int`, `0xFFFFFFFF + 1` would shift a bit into the sign position, which is undefined.
- **Divide with shifts (LeetCode 29).** Long division in binary: for `s` from 31 down to 0, if `b << s` fits under what is left of `a`, subtract it and set bit `s` of the quotient. 32 steps. The edge cases are exactly the ones from C 01: `INT_MIN / -1` overflows (the problem asks for `INT_MAX`), the magnitude of `INT_MIN` does not fit in an `int` (use a 64-bit unsigned for the working values), and negating the quotient must be done in unsigned arithmetic because the quotient can be 2^31 (for `INT_MIN / 1`).
- **Multiply with shifts and adds (Russian peasant).** For each set bit of `b`, add `a` shifted by that bit's position. The same loop with `%` after each add is modular multiplication, used in [number theory](02_number_theory_and_arithmetic.md) when `a * b` would overflow.
- **AND of a range (LeetCode 201).** Between `lo` and `hi`, every bit below the common prefix takes both values somewhere in the range, so those bits end up 0. Only the common prefix of `lo` and `hi` survives:

```
T9 range_and(5,7)=4 range_and(26,30)=24 range_and(0,2147483647)=0
```

  `5 & 6 & 7 = 4`, `26 & ... & 30 = 24`, and the whole range `[0, 2^31 - 1]` gives 0. **The test found a bug here.** My first version returned `lo << s` for the `[0, 0xFFFFFFFF]` case, where `s` reaches 32, and UBSan reported `shift exponent 32 is too large for 32-bit type`. The fix, `s >= 32 ? 0 : lo << s`, is in the code above. This is the "shift by the width" trap from C 01, caught by the sanitizer rather than by me.
- **Maximum XOR of two numbers (LeetCode 421).** Build the answer from the top bit down. Suppose the best `b` high bits are known as `best`. Try `cand = best | (1 << b)`: it is achievable exactly when two of the numbers' prefixes (their top bits) XOR to `cand`. Since `p ^ q = cand` means `q = p ^ cand`, a set (here a sorted vector) answers that in `O(log n)` per prefix. Total `O(32 n log n)`. The usual alternative is a binary trie, `O(32 n)`, see [tries](../07_trees/00_plan.md).

## 9. Bitsets, packed fields and a note on storage

- `std::bitset<N>` is a fixed-size array of bits with `&`, `|`, `^`, shifts, `count()`, `any()`, `_Find_first()`. Use it when `N` is known at compile time. It makes `O(n^2 / 64)` algorithms (reachability, subset-sum, see [the knapsack bitset](../10_dynamic_programming/04_the_knapsack_family.md)) fast enough. `std::vector<bool>` is a packed dynamic array, with the odd property that its elements are proxy objects, not real `bool&`.
- **Packing fields** into one word (a flags byte, a 12-bit id and a 4-bit type) is done with masks and shifts: `(w >> 4) & 0xFFF` extracts the id. In C the layout of a **bit-field struct** is implementation-defined; for file formats and registers use explicit shifts and masks, which is what drivers do (see the C track).
- **Endianness** only matters when you view the same bytes as different widths; shifts operate on values and are not affected.

## 10. Recognising the problem

| The statement says | Reach for |
|---|---|
| "every element appears twice except..." | XOR everything; for two singles, split on a differing bit |
| "every element appears 3 times except one" | count each bit modulo 3 |
| "is it a power of two / four" | `x & (x - 1)`, plus a position mask for four |
| "number of 1 bits", "Hamming weight/distance" | `popcount`, `x &= x - 1` |
| "sum of distances over all pairs of bit patterns" | per bit position, `c * (n - c)` |
| "all subsets", `n <= 20` | loop over `0 .. 2^n - 1` |
| "all subsets of subsets", "partition into groups" | submask enumeration, `3^n`, `n <= 18` |
| "choose exactly `k`" | Gosper's hack |
| "sum or max over all subsets of every mask" | SOS DP, `n * 2^n` |
| "without `+`, `*`, `/`" | XOR-and-carry, shift-and-add, shift-and-subtract |
| "AND/OR of a range" | common prefix |
| "maximum XOR pair" | greedy from the top bit with a set or trie |
| "adjacent states differ by one bit" | Gray code |
| "next number with the same number of 1 bits" | Gosper's formula |

## 11. Common mistakes

1. **Writing `1 << k` on an `int`.** UB at `k = 31`, and for a 64-bit mask it silently builds a 32-bit value. Use `1ull << k`.
2. **Shifting by 32 or more** (for example when the shift amount is `len` and `len` can equal the width). Guard it, as `range_and` had to.
3. **Operator precedence:** `x & 1 == 0`, or `a & b != 0`. Parenthesise.
4. **`ctz` or `clz` of zero:** undefined. Test `x != 0` first.
5. **`x & (x - 1) == 0` as the power-of-two test without `x != 0`:** reports 0 as a power of two.
6. **Signed right shift as division:** `-7 >> 1` is -4, while `-7 / 2` is -3.
7. **Using `int` for the carry loop in `add_no_plus`:** shifting a bit into the sign is undefined.
8. **Assuming the XOR swap is free:** it is slower than a temporary and breaks when both operands are the same object.
9. **Forgetting that the `3^n` enumeration is exponential** and quoting it as `O(2^n)`.
10. **Mixing `popcount` types:** `__builtin_popcount` takes `unsigned int`; for 64-bit values call `__builtin_popcountll`, or you silently drop the upper half.

## 12. Questions to answer out loud

1. Why does `x & (x - 1)` clear the lowest set bit? Say what subtracting 1 does to the bits.
2. How does `x & -x` isolate the lowest set bit? What is `-x` in two's complement?
3. Count the set bits of a 32-bit number three ways and compare their costs.
4. Every number appears twice except one: find it in `O(n)` time and `O(1)` space. What if two numbers are single? What if the rest appear three times?
5. How many subsets does a set of `n` items have? How many (mask, submask) pairs are there over all masks of `n` bits? Why `3^n`?
6. How do you enumerate all submasks of a mask? Why does `(sub - 1) & mask` work?
7. Add two integers with no `+`. Why does it terminate?
8. What is the AND of all integers in `[l, r]`?
9. Why is `1 << 31` on an `int` a problem, and how do you write it safely?
10. Describe the Gray code and one use.

## 13. Problems

### 13.1 On paper

Answer first, then run the code in section 1 to 8 to check. All numbers are 32-bit unsigned.

**A.** What is `0xB0 & (0xB0 - 1)`?

**B.** Is 4096 a power of four? Is 2048?

**C.** What is `0 ^ 1 ^ 2 ^ ... ^ 100`?

**D.** What is `0x5C & -0x5C`?

**E.** What is the Gray code of 13? What number has Gray code 11?

**F.** How many submasks does `10100` (binary) have, including itself and the empty set?

**G.** How many 5-bit masks have exactly 2 bits set?

**H.** What is `popcount(0xFF) * popcount(0x8001)`?

**I.** What are `ceil_pow2(1000)` and `floor_pow2(1000)`?

**J.** The array is `{0, 1, 3, 4}`, which should hold `0..4`. Which number is missing?

**K.** `x = 6, y = 3`, apply `x ^= y; y ^= x; x ^= y;`. What are `x` and `y`?

### 13.2 On LeetCode

Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | What to practise |
|---|---|---|
| Number of 1 Bits | 191 | `x &= x - 1` loop |
| Power of Two | 231 | `x != 0` guard |
| Power of Four | 342 | add the even-position mask |
| Counting Bits | 338 | `bits[i >> 1] + (i & 1)` |
| Single Number | 136 | XOR everything |
| Single Number II | 137 | count bits modulo 3 |
| Single Number III | 260 | split on a differing bit |
| Missing Number | 268 | XOR indices and values |
| Reverse Bits | 190 | five-step swap, or bit by bit |
| Hamming Distance | 461 | `popcount(x ^ y)` |
| Total Hamming Distance | 477 | per bit, `c * (n - c)` |
| Bitwise AND of Numbers Range | 201 | common prefix |
| Sum of Two Integers | 371 | XOR and carry |
| Divide Two Integers | 29 | shift and subtract, mind `INT_MIN` |
| Maximum XOR of Two Numbers in an Array | 421 | greedy from the top |
| Subsets | 78 | loop over masks |
| Gray Code | 89 | `i ^ (i >> 1)` |
| UTF-8 Validation | 393 | masks for the leading bits |

### 13.3 In your editor

**A.** Write `uint32_t next_same_popcount(uint32_t x)` using the Gosper formula. Check it against a loop that increments until the popcount matches.

**B.** Write `isolate_highest_set(x)` using `clz` and also without it (smear the bits right with `x |= x >> 1; x |= x >> 2; ...`, then `x - (x >> 1)`).

**C.** Time `popcount_loop`, `popcount_kernighan`, `popcount_swar` and the builtin on your Mac, with and without `-march=native`. Compare with the table in section 3.

**D.** Implement a `bitset` class on a `vector<uint64_t>` with `set`, `test`, `count`, `and_with` and `shift_left`. Compare `count` with `std::bitset`.

**E.** Write `all_pairs_hamming_bruteforce` and compare with `total_hamming` on random arrays. Then count how many pairs of the arrays have distance exactly 1.

### Answers

**A.** `0xA0` (160). Printed `160`.

**B.** 4096 = 2^12, even position, so yes. 2048 = 2^11, odd position, so no. Printed `1 0`.

**C.** `100 mod 4 = 0`, so the XOR is `n` itself: `100`.

**D.** `0x5C = 01011100`; the lowest set bit is bit 2, so `4`.

**E.** `13 ^ (13 >> 1) = 1101 ^ 0110 = 1011 = 11`. The inverse of Gray code 11 is `13`. Printed `11 13`.

**F.** `10100` has two set bits, so `2^2 = 4` submasks: `10100`, `10000`, `00100`, `00000`.

**G.** `C(5, 2) = 10`.

**H.** `8 * 2 = 16`.

**I.** `ceil_pow2(1000) = 1024`, `floor_pow2(1000) = 512`.

**J.** `2`.

**K.** `3 6`. The XOR swap swaps them.

The program that checks all of them printed:

```
A 160
B 1 0
C 100
D 4
E 11 13
F 4
G 10
H 16
I 1024 512
J 2
K 3 6
```

## Cheat sheet

- **Unsigned only, `1u << k`, count `0 .. width-1`.** Parenthesise every bit expression.
- **`x & (x - 1)`** clears the lowest set bit. **`x & -x`** isolates it. **`x | (x + 1)`** sets the lowest clear bit.
- **Power of two:** `x != 0 && !(x & (x - 1))`. **Power of four:** also `x & 0x55555555`.
- **Popcount:** Kernighan (few bits), SWAR or the builtin (many).
- **`31 - clz(x)`** is `floor(log2 x)`. `ctz` is the lowest set index. Both undefined at 0.
- **XOR cancels pairs.** Single: XOR all. Two singles: split on a bit of `x ^ y`. Thrice: count bits mod 3.
- **`xor_upto(n)`** is `n, 1, n + 1, 0` by `n mod 4`.
- **Pairs and bits:** treat each bit independently; `c * (n - c)`.
- **Subsets:** `0 .. 2^n - 1`. **Submasks:** `sub = (sub - 1) & mask`, `3^n` in total. **Exactly `k`:** Gosper. **Sum over subsets:** SOS, `n * 2^n`.
- **Add:** XOR plus `(a & b) << 1`, repeat. **AND of a range:** common prefix.

---

## Sources

- All code is tested here (5,282,884 checks, GCC 13.3, sanitizers on); outputs and times are measured on one Xeon container, two runs each. Times are this machine's.
- The identities, SWAR popcount, Gosper's hack and the sum over subsets are standard; see Sean Eron Anderson's "Bit Twiddling Hacks" (Stanford, free online), Knuth's *The Art of Computer Programming* volume 4A (bitwise tricks and techniques), and Warren, *Hacker's Delight*. The explanations and examples here are my own.
- Problem numbers and titles in section 13.2 are LeetCode's, from memory; check a number before you rely on it.
- Statements about ARM64 having a popcount instruction and about the C++20 `std::popcount` are from memory and not measured here.
