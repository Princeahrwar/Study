# DSA 11.02: Number Theory and Arithmetic

*Part of the [Bits and math plan](00_plan.md) · Time: about 6 hours (3 reading, 3 problems) · You need: [Bit manipulation](01_bit_manipulation.md) (the shift-and-add loop reappears as fast exponentiation), [Binary search on the answer](../03_binary_search_and_sorting/02_binary_search_on_the_answer.md) (for integer square root), and C 01 (why `a * b` can overflow before you widen it)*

Interview number theory is a small toolbox used over and over: the greatest common divisor, arithmetic **modulo** a number, fast exponentiation, the modular inverse (which is how you "divide" modulo a prime), the sieve of Eratosthenes, factorisation, and a few ways to do arithmetic without overflowing. This tutorial builds each from its reason, tests it, and ends with the primality test (Miller-Rabin) and the integer square root (Newton's method).

By the end you can:

- compute a gcd, an lcm without overflow, and the numbers `x`, `y` in `a*x + b*y = gcd(a, b)`;
- do arithmetic modulo `m` without a negative result or an overflow, and compute `b^e mod m` in `O(log e)`;
- find a modular inverse three ways and say when each one applies, and divide modulo a prime;
- sieve all primes up to `n`, factor with a smallest-prime-factor table, and test a 64-bit number for primality exactly;
- write `Pow(x, n)` and an integer square root that survive `INT_MIN` and `UINT64_MAX`;
- spot the overflow in a midpoint, a ceiling division and an `lcm`.

**Where the numbers come from.** The code below was compiled with `-Wall -Wextra` and tested: 5,746,565 checks passed under AddressSanitizer and UBSan (GCC 13.3). The tests ran: gcd against a divisor loop for every pair up to 200 and against `std::gcd` on 200,000 random pairs; `ext_gcd` against Bezout's identity; `mulmod` against the portable double-and-add version on 200,000 random moduli up to 64 bits; `powmod` against repeated multiplication; every inverse against `a * inv % m == 1`, for both a prime and a composite modulus (including "no inverse" cases); the two sieves against each other and a third sieve up to 2,000,000, with the count 148,933 pinned; factoring against the reconstructed product; `count_divisors` and `euler_phi` against counting loops up to 3,000; Miller-Rabin against trial division for every number below 300,000 and for 20,000 random numbers below 10^12, plus Carmichael numbers, strong pseudoprimes and known large primes; `pow_fast` against `std::pow`; and both square roots at the edges (`INT_MAX`, `UINT64_MAX`) and on 300,000 random values.

**Data and timing.** Inputs are synthetic and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are the **best of 3 repetitions, shown for run 1 / run 2**; they are this machine's, not yours. Counts and traces are the same in both runs.

---

## 1. Greatest common divisor and least common multiple

The **gcd** of `a` and `b` is the largest number that divides both. **Euclid's algorithm** is `gcd(a, b) = gcd(b, a mod b)` until the second number is 0. It works because any number that divides both `a` and `b` also divides `a - q*b`, which is `a mod b`, and the other way round; so the pair `(a, b)` and the pair `(b, a mod b)` have exactly the same common divisors.

```cpp
// Euclid: gcd(a, b) = gcd(b, a mod b), because any common divisor of a and b divides a - q*b = a mod b, and the other way round.
// Each two steps at least halve the larger number, so there are at most about 2 log2(max) steps.
u64 gcd_euclid(u64 a, u64 b) { while (b) { u64 t = a % b; a = b; b = t; } return a; }
// LCM: divide FIRST, then multiply.  a / gcd * b never exceeds the true lcm, while a * b may overflow before the division.
u64 lcm_safe(u64 a, u64 b) { return (a == 0 || b == 0) ? 0 : a / gcd_euclid(a, b) * b; }
// Extended Euclid: returns g = gcd(a, b) and x, y with a*x + b*y = g (Bezout's identity).
struct Bezout { long long g, x, y; };
Bezout ext_gcd(long long a, long long b) {
    if (b == 0) return {a, 1, 0};
    Bezout r = ext_gcd(b, a % b);                 // b*r.x + (a mod b)*r.y = g, and a mod b = a - (a/b)*b
    return {r.g, r.y, r.x - (a / b) * r.y};
}
int gcd_steps(u64 a, u64 b) { int s = 0; while (b) { u64 t = a % b; a = b; b = t; ++s; } return s; }
```

The trace on `gcd(1071, 462)`:

```
T1 gcd(1071,462) steps: (1071,462)-> (462,147)-> (147,21)-> 21
```

Three steps. Each pair is `(larger, smaller)`; the remainder of 1071 by 462 is 147, then 462 by 147 is 21, then 147 by 21 is 0, so the answer is 21.

**How many steps?** After two steps the larger number has at least halved, so there are at most about `2 log2(max)` steps. The worst case is **consecutive Fibonacci numbers** (every quotient is 1). Measured for the 92nd and 91st Fibonacci numbers, and for 100,000 random 64-bit pairs:

```
T3 gcd_steps fib92,fib91=90 random64 avg=37.5 max=62 (2*log2(2^64)=128)
```

So the worst case is 90 steps for 64-bit input and a typical pair takes about 38. Either way Euclid is effectively constant time; it is `O(log min(a, b))`.

**LCM and overflow.** `lcm(a, b) = a * b / gcd(a, b)`, but `a * b` can overflow even when the lcm fits. Write `a / gcd(a, b) * b`: the division is exact, and the result never exceeds the true lcm. (The same instinct: reduce first, then multiply.)

**Extended Euclid.** Run Euclid, then work back up the calls to find integers `x`, `y` with `a*x + b*y = gcd(a, b)` (**Bezout's identity**). At each level, if `b*x' + (a mod b)*y' = g` then, since `a mod b = a - (a/b)*b`, we get `a*y' + b*(x' - (a/b)*y') = g`. That is the line in `ext_gcd`. For `a = 240, b = 46`:

```
T2 ext_gcd(240,46): g=2 x=-9 y=47 check=2
```

`240 * (-9) + 46 * 47 = -2160 + 2162 = 2`, the gcd. The coefficients are not unique; this is the pair Euclid produces. They matter in section 3.

Uses: reducing a fraction; the gcd of a whole array (fold with gcd); LeetCode 365 (water jugs): you can measure exactly `target` litres iff `target` is a multiple of `gcd(x, y)` and `target <= x + y`; LeetCode 1071 (gcd of strings): the answer exists iff `a + b == b + a`, and its length is `gcd(len a, len b)`.

## 2. Modular arithmetic

Many problems ask for an answer "modulo `10^9 + 7`" because the true answer is astronomically large. The rules are the ones from [DP 08](../10_dynamic_programming/08_counting_digit_dp_and_optimisations.md), stated for any modulus `m`:

- `(a + b) mod m = ((a mod m) + (b mod m)) mod m`, and the same for `*`.
- Subtraction can go negative: use `((a - b) % m + m) % m`, because in C and C++ the remainder takes the sign of the dividend.
- **Division is not allowed.** `a / b mod m` is `a * inverse(b) mod m`, and the inverse exists only when `gcd(b, m) = 1` (section 3).
- You cannot compare two reduced numbers to say which true value is larger.

**Why `10^9 + 7`?** It is a prime (needed for Fermat's inverse), it fits in 30 bits, and the product of two numbers below it, about `10^18`, fits in a signed 64-bit integer (the maximum is about `9.2 * 10^18`). So `a * b % m` is safe with plain `long long`. Another common modulus is `998244353`.

**When `m` is large.** If `m` is near `2^64`, even `a * b` overflows 64 bits. Two fixes: use a 128-bit intermediate (`unsigned __int128`, a GCC and Clang extension, not standard C++), or use the portable double-and-add, which is exactly the shift-and-add multiplication of [bit manipulation 8](01_bit_manipulation.md) with a modular add that cannot overflow:

```cpp
// Modular arithmetic: reduce after every operation.  (a + b) % m, (a * b) % m are fine; a - b needs + m; division needs an inverse.
inline u64 addmod(u64 a, u64 b, u64 m) { return a >= m - b ? a - (m - b) : a + b; }   // a, b < m; never overflows, even for m > 2^63
inline u64 submod(u64 a, u64 b, u64 m) { return a >= b ? a - b : a + (m - b); }        // a, b < m
inline u64 mulmod(u64 a, u64 b, u64 m) { return (u64)((u128)a * b % m); }               // a * b needs 128 bits when m is near 2^64
u64 mulmod_portable(u64 a, u64 b, u64 m) {                                              // the same without __int128: double-and-add
    u64 r = 0; a %= m;
    while (b) { if (b & 1) r = addmod(r, a, m); a = addmod(a, a, m); b >>= 1; }
    return r;
}
// Fast exponentiation: b^e mod m in O(log e) multiplications.  Square the base, multiply the result in for each set bit of e.
u64 powmod(u64 b, u64 e, u64 m) {
    u64 r = 1 % m; b %= m;
    while (e) { if (e & 1) r = mulmod(r, b, m); b = mulmod(b, b, m); e >>= 1; }
    return r;
}
```

The 128-bit version is far faster. Ten million `mulmod` calls with a modulus near `2^64` (milliseconds, run 1 / run 2): `__int128` 63.5 / 57.1, portable double-and-add 2293.0 / 2513.6. In an interview, use the 128-bit type when the platform allows it and say what the fallback is.

**Fast exponentiation.** To compute `b^e mod m` do not multiply `e` times. Write `e` in binary, square the base at each bit, and multiply the result in whenever the bit is 1: `O(log e)` multiplications. The trace for `3^13 mod 1000` (13 is `1101` in binary):

```
T4 3^13 mod 1000 by squaring: e=13=1101b [r=3 b=9] [r=3 b=81] [r=243 b=561] [r=323 b=721] => 323 (direct 323)
```

Read each bracket as the state after one bit: the bits of 13 from the lowest are 1, 0, 1, 1. After the first bit, `r = 3` and `b = 9`; the second bit is 0 so `r` stays 3 while `b` becomes 81; the third bit is 1 so `r = 3 * 81 = 243` and `b = 561`; the fourth multiplies `243 * 561 mod 1000 = 323`. The answer, 323, equals the direct computation. One million `powmod(i, 10^9 + 5, 10^9 + 7)` calls took 167.0 / 162.0 ms (each does about 30 squarings).

The same loop with matrices instead of numbers computes Fibonacci in `O(log n)`, and with any associative operation in place of multiplication it is the same doubling idea behind **binary lifting**.

## 3. The modular inverse

The **inverse** of `a` modulo `m` is the number `x` with `a * x = 1 (mod m)`. It exists exactly when `gcd(a, m) = 1`: from `a*x + m*y = 1` (Bezout) we read `a*x = 1 (mod m)`, and conversely.

```cpp
// The inverse of a modulo m is x with a*x = 1 (mod m).  It exists exactly when gcd(a, m) = 1.
// Way 1: extended Euclid, for any m.  From a*x + m*y = 1 we read a*x = 1 (mod m).  Returns -1 when no inverse exists.
long long modinv_ext(long long a, long long m) {
    a %= m; if (a < 0) a += m;
    Bezout r = ext_gcd(a, m);
    if (r.g != 1) return -1;
    return ((r.x % m) + m) % m;
}
// Way 2: Fermat's little theorem, for a PRIME p and a not divisible by p: a^(p-1) = 1, so a^(p-2) is the inverse.
u64 modinv_prime(u64 a, u64 p) { return powmod(a, p - 2, p); }
// Way 3: all inverses 1..n at once, for a prime p > n.  From p = (p/i)*i + (p mod i):  inv[i] = -(p/i) * inv[p mod i]  (mod p).
std::vector<u32> inverses_upto(int n, u32 p) {
    std::vector<u32> inv(n + 1, 0); inv[1] = 1;
    for (int i = 2; i <= n; ++i) inv[i] = (u32)((p - (u64)(p / i) * inv[p % i] % p) % p);
    return inv;
}
// Division modulo a prime: a / b = a * inverse(b).
u64 divmod_prime(u64 a, u64 b, u64 p) { return mulmod(a % p, modinv_prime(b % p, p), p); }
```

Three ways:

1. **Extended Euclid.** Works for any `m`. Run `ext_gcd(a, m)`; if the gcd is not 1, there is no inverse; otherwise `x` (made non-negative) is the answer.
2. **Fermat's little theorem.** For a prime `p` and `a` not divisible by `p`, `a^(p-1) = 1 (mod p)`, so `a^(p-2)` is the inverse: one `powmod`. Simple, but only for a prime modulus. (For composite `m` the exponent is Euler's `phi(m) - 1`, which needs the factorisation of `m`.)
3. **All inverses `1..n` at once**, for a prime `p > n`: `p = (p/i)*i + (p mod i)` gives `inv[i] = -(p/i) * inv[p mod i] (mod p)`. `O(n)` total instead of `O(n log p)`; used when you need `1/1, 1/2, ..., 1/n`, such as binomial coefficients with a precomputed table.

Measured:

```
T5 modinv_ext(3,11)=4 modinv_ext(4,8)=-1 modinv_ext(-3,11)=7 modinv_prime(3,11)=4
T6 inverses mod 13: 1 7 9 10 8 11 2 5 3 4 6 12
```

`3 * 4 = 12 = 1 (mod 11)`, so the inverse of 3 is 4. `4` has no inverse modulo 8 (`gcd(4, 8) = 4`), so the function returns -1. The inverse of `-3` modulo 11 is 7, the same as the inverse of 8 (`8 * 7 = 56 = 1 + 5*11`). The table of inverses modulo 13 reads: the inverse of 2 is 7 (`14 = 1 + 13`), of 3 is 9 (`27 = 1 + 2*13`), and so on; inverses of 1 and 12 are themselves (`12 * 12 = 144 = 1 + 11*13`).

**Division modulo a prime:** `divmod_prime(a, b, p) = a * inverse(b)`. For example `10 / 4 mod 13`: the inverse of 4 is 10 (`40 = 1 + 3*13`), so the answer is `10 * 10 mod 13 = 9`. Check: `9 * 4 = 36 = 10 + 2*13`.

## 4. Primes and the sieve

A prime has exactly two divisors. To test one number, try divisors up to its square root (a composite `n` has a divisor `d <= sqrt n`): `O(sqrt n)`. To list **all** primes up to `n`, use the **sieve of Eratosthenes**: for each prime `p`, cross out its multiples starting from `p*p` (smaller multiples have a smaller prime factor and were crossed out already).

```cpp
// Sieve of Eratosthenes: for each prime p, cross out p*p, p*p + p, ...  (smaller multiples were crossed out by smaller primes).
// Work: n/2 + n/3 + n/5 + ... = about n ln ln n.
std::vector<int> primes_up_to(int n) {
    std::vector<char> comp(n + 1, 0); std::vector<int> pr;
    for (long long i = 2; i <= n; ++i) {
        if (comp[i]) continue;
        pr.push_back((int)i);
        for (long long j = i * i; j <= n; j += i) comp[j] = 1;
    }
    return pr;
}
// Linear sieve: every composite is crossed out exactly once, by its smallest prime factor.  Also gives spf[x] for factoring.
struct LinearSieve {
    std::vector<int> spf, primes;
    explicit LinearSieve(int n) : spf(n + 1, 0) {
        for (int i = 2; i <= n; ++i) {
            if (spf[i] == 0) { spf[i] = i; primes.push_back(i); }
            for (int p : primes) {
                if (p > spf[i] || (long long)i * p > n) break;     // stop at spf[i]: i * p's smallest prime factor is then p
                spf[i * p] = p;
            }
        }
    }
    std::vector<std::pair<int,int>> factorize(int x) const {      // O(log x) once the table exists
        std::vector<std::pair<int,int>> f;
        while (x > 1) { int p = spf[x], c = 0; while (x % p == 0) { x /= p; ++c; } f.push_back({p, c}); }
        return f;
    }
};
```

**Cost.** The sieve does about `n/2 + n/3 + n/5 + ...` crossings, which is `n ln ln n`: in practice a small constant times `n`. Measured crossings against the formula `N ln ln N` (an estimate, since the sieve starts at `p*p`):

```
T17 N=1000000 primes=78498 simple_crossings=2122048  composites=921501  N*lnlnN=2625792
T17 N=10000000 primes=664579 simple_crossings=22850051  composites=9335420  N*lnlnN=27799426
```

There are 78,498 primes below one million and 664,579 below ten million. The sieve makes about 2.3 to 2.4 times as many crossings as there are composites, because a composite is crossed once for each of its distinct prime factors `p` with `p*p` at most the composite.

**Linear sieve.** The second version crosses out every composite **exactly once**, by its **smallest prime factor** (`spf`), by multiplying each `i` only by primes up to `spf[i]`. It also leaves the `spf` table, which gives fast factorisation. The table for 2 to 20:

```
T10 spf table 2..20: 2:2 3:3 4:2 5:5 6:2 7:7 8:2 9:3 10:2 11:11 12:2 13:13 14:2 15:3 16:2 17:17 18:2 19:19 20:2
```

Timing (milliseconds, run 1 / run 2): primes up to `10^7`, simple sieve 53.8 / 55.8, linear sieve 55.5 / 55.6; the simple sieve up to `10^8` took 634.9 / 658.8. **The linear sieve is not faster than the simple one here**, the two are equal within noise at `10^7`. Its advantage is the `spf` table, not speed. For comparison, testing every number up to `10^6` by trial division took 128.8 / 106.9 ms, about twice as long as the whole simple sieve up to `10^7`.

Memory: a `vector<char>` for `10^8` is 100 MB; use a `vector<bool>` or a bitset (8 times smaller), skip even numbers, or a **segmented sieve** (sieve blocks of the range `[L, R]` using the primes up to `sqrt R`) when `R` is large but `R - L` is small.

LeetCode 204 (count primes) is exactly `primes_up_to(n - 1).size()`.

## 5. Factorisation, divisors, Euler's phi, and primality

**Trial division.** Try `d = 2, 3, ...` while `d * d <= n`; divide out each factor completely; whatever remains at the end is 1 or one prime. `O(sqrt n)`.

```cpp
// Trial division: try d = 2, 3, ... while d*d <= n.  Whatever is left over at the end is 1 or a single prime.  O(sqrt n).
std::vector<std::pair<u64,int>> factorize(u64 n) {
    std::vector<std::pair<u64,int>> f;
    for (u64 d = 2; d * d <= n; ++d) {
        if (n % d) continue;
        int c = 0; while (n % d == 0) { n /= d; ++c; }
        f.push_back({d, c});
    }
    if (n > 1) f.push_back({n, 1});
    return f;
}
u64 count_divisors(u64 n) { u64 r = 1; for (auto& pe : factorize(n)) r *= pe.second + 1; return r; }   // product of (exponent + 1)
u64 euler_phi(u64 n) {                                // count of 1..n coprime to n: n * product of (1 - 1/p) over the primes p | n
    u64 r = n; for (auto& pe : factorize(n)) r = r / pe.first * (pe.first - 1); return r;
}
// Miller-Rabin, deterministic for all 64-bit n with these 12 bases.  n - 1 = d * 2^s.  If n is prime, then for each base a, either
// a^d = 1 or a^(d * 2^r) = -1 for some r < s.  A composite that passes for a base is a "strong pseudoprime" to that base.
bool is_prime(u64 n) {
    if (n < 2) return false;
    for (u64 p : {2ull, 3ull, 5ull, 7ull, 11ull, 13ull, 17ull, 19ull, 23ull, 29ull, 31ull, 37ull}) {
        if (n == p) return true;
        if (n % p == 0) return false;
    }
    u64 d = n - 1; int s = 0; while ((d & 1) == 0) { d >>= 1; ++s; }
    for (u64 a : {2ull, 3ull, 5ull, 7ull, 11ull, 13ull, 17ull, 19ull, 23ull, 29ull, 31ull, 37ull}) {
        u64 x = powmod(a, d, n);
        if (x == 1 || x == n - 1) continue;
        bool witness = true;
        for (int r = 1; r < s && witness; ++r) { x = mulmod(x, x, n); if (x == n - 1) witness = false; }
        if (witness) return false;
    }
    return true;
}
```

```
T8 360 = 2^3 3^2 5^1  divisors=24 phi(36)=12 phi(97)=96
T9 600851475143 = 71 839 1471 6857
```

`360 = 2^3 * 3^2 * 5`, and its number of divisors is `(3 + 1)(2 + 1)(1 + 1) = 24`. **Euler's phi** `phi(n)` counts the numbers in `1..n` coprime to `n`: `n` times `(1 - 1/p)` for each prime `p` dividing it, so `phi(36) = 36 * 1/2 * 2/3 = 12`, and for a prime `phi(p) = p - 1` (`phi(97) = 96`). The factorisation of `600851475143` (Project Euler's classic) has largest prime factor 6857.

With the `spf` table from section 4, factoring any `x <= n` takes `O(log x)` time: repeatedly divide by `spf[x]`. Use it when you factor many numbers.

**Miller-Rabin primality test.** Trial division at `n ~ 10^18` needs `10^9` divisions. **Miller-Rabin** is a probabilistic test that becomes **deterministic** for 64-bit numbers with a fixed set of bases. Write `n - 1 = d * 2^s` with `d` odd. If `n` is prime, then for any base `a` either `a^d = 1 (mod n)` or `a^(d * 2^r) = -1 (mod n)` for some `0 <= r < s` (because the only square roots of 1 modulo a prime are 1 and -1). A composite that passes for a base `a` is a **strong pseudoprime** to base `a`. The code above uses the 12 bases 2 to 37, which is known to be exact for all `n < 3.18 * 10^23`, so for all 64-bit numbers.

A single base is not enough. The number `3215031751 = 151 * 751 * 28351` passes the test for bases 2, 3, 5 and 7, and is caught only by 11:

```
T11 n=3215031751 (=151*751*28351) n-1=d*2^1 d=1607515875; strong-probable-prime to bases: 2:pass 3:pass 5:pass 7:pass 11:FAIL  is_prime=0
```

and the full `is_prime` correctly returns 0 (the last field). Large inputs:

```
T12 is_prime(1e18+3)=1 is_prime(2^61-1)=1 is_prime(2^64-59)=1 is_prime(561)=0
```

`10^18 + 3`, `2^61 - 1` and `2^64 - 59` (the largest prime below `2^64`) are prime; the Carmichael number 561 is not. One million calls of `is_prime` on the numbers just above `10^18` took 209.6 / 191.2 ms, and it found:

```
T18 primes in [1e18,1e18+1e6)=24280 (expected about 1e6/ln(1e18)=24127)
```

24,280 primes against an estimate of 24,127 from the prime number theorem (density `1 / ln n`). The test costs about 0.2 microseconds per call here, most likely because most numbers are rejected by the small-prime checks before any `powmod` (I did not measure the split). For factoring a large number (not just testing), the next tool is **Pollard's rho**; I do not implement it here, but it is the standard answer to "factor a 64-bit number".

## 6. Overflow-safe arithmetic

```cpp
// Overflow-safe small tools.
inline u32 midpoint_floor(u32 a, u32 b) { return (a & b) + ((a ^ b) >> 1); }       // floor((a + b) / 2) with no 33-bit intermediate
inline u64 ceil_div(u64 a, u64 b) { return a / b + (a % b != 0); }                  // (a + b - 1) / b can overflow
inline bool mul_fits_u64(u64 a, u64 b) { return a == 0 || b <= UINT64_MAX / a; }
long long trailing_zeros_of_factorial(long long n) { long long c = 0; while (n) { n /= 5; c += n; } return c; }   // n! has this many trailing zeros
```

- **Midpoint.** `(a + b) / 2` overflows when `a + b` does not fit. For two unsigned 32-bit values of 4 billion, the sum wraps and the "average" is wrong; `(a & b) + ((a ^ b) >> 1)` (the shared bits plus half the differing bits) never overflows:

```
T7 a=b=4e9: (a+b)/2 in u32=1852516352  midpoint_floor=4000000000
```

  The usual fix for binary search is `lo + (hi - lo) / 2`, which needs `lo <= hi`.
- **Ceiling division.** `(a + b - 1) / b` overflows near the maximum; `a / b + (a % b != 0)` does not.
- **Does `a * b` fit?** `b <= UINT64_MAX / a` when `a != 0`: divide the limit instead of multiplying. (For signed values, use the compiler builtins from C 01.)
- **Trailing zeros of `n!`** (LeetCode 172). Each trailing zero is a factor 10 = 2 * 5, and there are more 2s than 5s, so count the 5s: `n/5 + n/25 + n/125 + ...`. No factorial is computed:

```
T16 trailing zeros of 100!=24  of 10^9!=249999998
```

## 7. `Pow(x, n)`

LeetCode 50: compute `x^n` for a real `x` and an integer `n`, possibly negative. The same loop as `powmod`, with three traps: put the exponent in a `long long` before negating (`-INT_MIN` overflows an `int`); take the reciprocal of `x` for a negative exponent; and the result can underflow to 0 or overflow to infinity, which is the correct answer.

```cpp
// x^n for a real x and an int n (LeetCode 50).  Fast exponentiation again.  The exponent goes into a long long first:
// -INT_MIN does not fit in an int.  A negative exponent is the reciprocal of the positive one.
double pow_fast(double x, int n) {
    long long e = n; if (e < 0) { x = 1.0 / x; e = -e; }
    double r = 1.0;
    while (e) { if (e & 1) r *= x; x *= x; e >>= 1; }
    return r;
}
```

```
T15 pow_fast(2,-2)=0.25 pow_fast(2,INT_MIN)=0 pow_fast(-1,INT_MIN)=1 pow_fast(2,10)=1024
```

`pow_fast(2, INT_MIN)` is 0 (underflow), and `pow_fast(-1, INT_MIN)` is 1 (an even exponent). The recursion `x^n = (x^(n/2))^2 * (n odd ? x : 1)` is equivalent and also `O(log n)`.

## 8. Integer square root

The largest `r` with `r*r <= n`. Two methods.

**Binary search on the answer** (LeetCode 69): `r` lies between 0 and about `sqrt(max)`; the predicate `mid * mid <= x` is monotone; compute the product in a wider type. For a 32-bit `int` the bound is 46340, since `46340^2 = 2,147,395,600 <= INT_MAX < 46341^2`.

**Newton's method** for `f(r) = r^2 - n` iterates `r' = (r + n / r) / 2`. Starting from any value at or above the root, the sequence decreases and stops at `floor(sqrt n)`; the number of correct digits roughly doubles every step (quadratic convergence).

```cpp
// Integer square root: the largest r with r*r <= n.
int isqrt_binary(int x) {                                  // LeetCode 69.  The product mid*mid must be computed in a wider type.
    long long lo = 0, hi = 46340;                          // 46340^2 = 2147395600 <= INT_MAX < 46341^2
    while (lo < hi) { long long mid = (lo + hi + 1) / 2; if (mid * mid <= x) lo = mid; else hi = mid - 1; }
    return (int)lo;
}
// Newton's method on f(r) = r^2 - n:  r' = (r + n / r) / 2.  Start at or above the root; the sequence then decreases to
// floor(sqrt n) and stops the first time it would increase.  Converges quadratically: the number of correct digits doubles.
u64 isqrt_newton(u64 n, int* iters = nullptr) {
    if (n < 2) { if (iters) *iters = 0; return n; }
    int bits = 64 - __builtin_clzll(n);
    u64 x = 1ull << ((bits + 1) / 2);                      // 2^ceil(bits/2) >= sqrt(n)
    int it = 0;
    while (true) { u64 y = (x + n / x) / 2; ++it; if (y >= x) break; x = y; }
    if (iters) *iters = it;
    return x;
}
```

The trace for `n = 17`:

```
T13 isqrt_newton(17): x=8 y=5 y=4 y=4 => 4
```

Start at 8 (a power of two at or above the root), then 5, then 4; the next value is 4 again, so it stops and returns 4. The number of iterations stays tiny even for the largest inputs:

```
T14 isqrt_newton(1e18)=1000000000 in 4 iterations; isqrt_newton(2^64-1)=4294967295 in 2 iterations
```

Four iterations for `10^18` and two for `2^64 - 1` (the start `2^32` is already one above the root). Starting at or above the root is essential: a start below it makes the first step jump above, and the "stop when it increases" rule would then return too early. Do not use `(long long)sqrt((double)n)` for 64-bit `n`: a `double` has 53 bits of precision, so the result can be off by one near perfect squares (I did not measure this).

## 9. Recognising the problem

| The statement says | Reach for |
|---|---|
| "greatest common divisor", "can you measure exactly k", "split into equal pieces" | `gcd`, Bezout |
| "answer modulo 10^9 + 7" | reduce after every operation; `powmod`; inverse for division |
| "compute `a^b`, `b` huge" | fast exponentiation; reduce `b` modulo `phi` only when you can justify it |
| "n choose k mod p" | factorials and inverse factorials (next tutorial) |
| "count primes up to n", "primes in a range" | sieve; segmented sieve for a range |
| "factor many numbers" | smallest-prime-factor table |
| "is this 64-bit number prime" | Miller-Rabin |
| "number of divisors", "sum of divisors", "coprime count" | factorise, then the product formulas |
| "trailing zeros of n!" | count factors of 5 |
| "integer square root", "perfect square" | binary search or Newton, never a `double` for 64 bits |
| "average", "midpoint", "ceiling divide" near the type's maximum | the overflow-free forms in section 6 |

## 10. Common mistakes

1. **`a * b % m` with `a * b` overflowing** (for `m` near 2^63 or above), or with an `int` product: widen first, `(long long)a * b % m`.
2. **A negative remainder** after a subtraction: add `m` before the final `%`.
3. **Using Fermat's inverse with a composite modulus.** It is only valid for a prime.
4. **Dividing in a modular formula** instead of multiplying by the inverse.
5. **Computing `lcm` as `a * b / gcd`.**
6. **A sieve with `int i * i`** overflowing for `n` near 2^31: the code above uses `long long` for the index.
7. **Forgetting 0 and 1 are not prime**, and that 2 is the only even prime.
8. **Trial division that tests `d <= n / 2`** instead of `d * d <= n`: correct but far slower.
9. **`pow_fast` negating an `int` exponent** (`-INT_MIN` overflows).
10. **Taking `sqrt` of a 64-bit integer through a `double`.**
11. **A Miller-Rabin test with too few bases** (2, 3, 5, 7 alone accept 3215031751).
12. **Mixing signed and unsigned** in the modular code (see C 01).

## 11. Questions to answer out loud

1. State Euclid's algorithm and prove it. How many steps can it take, and which input is the worst case?
2. Why compute `a / gcd * b` and not `a * b / gcd`?
3. What does extended Euclid return? How do you get a modular inverse from it?
4. Compute `b^e mod m` in `O(log e)`. Why does squaring work?
5. When does a modular inverse exist? Give three ways to compute one and when each applies.
6. Why is `10^9 + 7` a popular modulus?
7. What is the cost of the sieve of Eratosthenes? Why does it start crossing out at `p*p`?
8. What does the linear sieve buy you if it is not faster?
9. How would you test a 64-bit number for primality? What goes wrong with two bases?
10. How many divisors does `n` have if `n = p1^a1 * ... * pk^ak`? What is `phi(n)`?
11. Write the integer square root with Newton's method. Where must it start, and why?
12. How do you average two integers that may be near the maximum?

## 12. Problems

### 12.1 On paper

Answer first, then run the code to check.

**A.** What are `gcd(84, 36)` and `lcm(84, 36)`?

**B.** Find the inverse of 7 modulo 40, and of 6 modulo 40.

**C.** Compute `2^10 mod 1000` and `7^100 mod 13` (hint: `7^12 = 1 mod 13`).

**D.** What are `phi(100)` and the number of divisors of 100?

**E.** How many primes are there below 30?

**F.** How many trailing zeros does `1000!` have?

**G.** What are `isqrt(99)` and `isqrt(10^18 - 1)`?

**H.** Is 91 prime? Is `10^9 + 7`?

**I.** What is the midpoint (rounded down) of 3,000,000,000 and 3,000,000,000 as unsigned 32-bit numbers, and what does `(a + b) / 2` give?

**J.** What is `2^-3`?

**K.** What is `10 / 4` modulo 13?

### 12.2 On LeetCode

Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | Where | How to approach |
|---|---|---|---|
| Count Primes | 204 | LeetCode | Sieve; start the inner loop at `i * i` |
| Pow(x, n) | 50 | LeetCode | Fast exponentiation with a `long long` exponent and a reciprocal for negative `n` |
| Sqrt(x) | 69 | LeetCode | Binary search with `mid * mid` in a wider type, or Newton |
| Super Pow | 372 | LeetCode | `a^(10*k + d) = (a^k)^10 * a^d`: process the digits of the exponent with `powmod`, modulus 1337 |
| Water and Jug Problem | 365 | LeetCode | Measurable iff `target <= x + y` and `gcd(x, y)` divides `target` |
| Greatest Common Divisor of Strings | 1071 | LeetCode | Check `s1 + s2 == s2 + s1`; the answer is the prefix of length `gcd(len1, len2)` |
| Factorial Trailing Zeroes | 172 | LeetCode | Count factors of 5: `n/5 + n/25 + ...` |
| Ugly Number II | 264 | LeetCode | Three pointers merging multiples of 2, 3, 5 (or a heap); factorisation practice |
| Prime Arrangements | 1175 | LeetCode | Count primes up to `n`; the answer is `primes! * (n - primes)!` modulo `10^9 + 7` |
| Perfect Number | 507 | LeetCode | Sum of divisors up to `sqrt(n)` in pairs |

### 12.3 In your editor

**A.** Write the modular exponentiation recursively (`x^n = (x^(n/2))^2 * ...`) and compare with `powmod` on random inputs.

**B.** Write a segmented sieve that lists the primes in `[10^12, 10^12 + 10^6]` and compare its count with `is_prime` over the same range.

**C.** Write `phi` for every `n <= N` in `O(N log log N)` using a sieve, and check it against `euler_phi`.

**D.** Pollard's rho: factor `10^18 + 9` (or any semiprime near 10^18) using `is_prime` to stop and `gcd` to split. Compare with trial division.

**E.** Use the Chinese remainder theorem for two coprime moduli: given `x = a (mod m)` and `x = b (mod n)`, find `x` modulo `m * n` using `modinv_ext`.

### Answers

The program that checks all of the paper problems printed:

```
A 12 252
B 23 -1
C 24 9
D 40 9
E 10
F 249
G 9 999999999
H 0 1
I 3000000000
J 0.125
K 9
```

Reading it line by line:

**A.** `gcd = 12`, `lcm = 84 / 12 * 36 = 252`.

**B.** `7 * 23 = 161 = 1 + 4*40`, so the inverse is `23`. `6` has none since `gcd(6, 40) = 2`; the function returns -1.

**C.** `2^10 = 1024`, so `24`. `100 mod 12 = 4` and `7^4 = 2401 = 9 + 184*13`, so `9`.

**D.** `100 = 2^2 * 5^2`: `phi = 100 * 1/2 * 4/5 = 40`; divisors `3 * 3 = 9`.

**E.** `2, 3, 5, 7, 11, 13, 17, 19, 23, 29`: `10`.

**F.** `1000/5 + 1000/25 + 1000/125 + 1000/625 = 200 + 40 + 8 + 1 = 249`.

**G.** `9` (81 <= 99 < 100) and `999999999`.

**H.** `91 = 7 * 13`, so `0`. `10^9 + 7` is prime: `1`.

**I.** `3,000,000,000` (both inputs agree). In 32-bit arithmetic `a + b` wraps to 1,705,032,704, so `(a + b) / 2` would give 852,516,352.

**J.** `0.125`.

**K.** `9`.

## Cheat sheet

- **Euclid:** `gcd(a, b) = gcd(b, a % b)`; `lcm = a / gcd * b`; `ext_gcd` gives `a*x + b*y = g`.
- **Modular:** reduce after each operation; add `m` after a subtraction; widen before multiplying; no division, use the inverse.
- **Fast power:** square the base, multiply in on set bits, `O(log e)`. The same loop does matrices and `Pow(x, n)`.
- **Inverse:** exists iff `gcd(a, m) = 1`. Extended Euclid (any `m`), Fermat `a^(p-2)` (prime `p`), linear table (prime `p > n`).
- **Sieve:** cross out from `p*p`, `n ln ln n`; the linear sieve gives `spf` for fast factoring.
- **Factoring:** trial division up to `sqrt n`; divisors `prod (e + 1)`; `phi = n prod (1 - 1/p)`.
- **Primality of a 64-bit number:** Miller-Rabin with the 12 bases 2 to 37.
- **Overflow:** midpoint `(a & b) + ((a ^ b) >> 1)`; ceiling `a / b + (a % b != 0)`; check `b <= MAX / a`.
- **Square root:** binary search (wide product) or Newton from at or above the root; never through a `double` for 64 bits.
- **Trailing zeros of `n!`:** `n/5 + n/25 + ...`.

---

## Sources

- All code is tested here (5,746,565 checks, GCC 13.3, sanitizers on); counts, traces and timings are measured on one Xeon container, two runs each. Times are this machine's.
- Euclid's algorithm and its worst case (Lamé's theorem), Bezout's identity, Fermat's little theorem, Euler's theorem, the sieve of Eratosthenes, Miller-Rabin and Newton's method are standard; see CLRS (the chapter on number-theoretic algorithms) and Knuth, *The Art of Computer Programming* volume 2. The claim that the 12 bases 2 to 37 are deterministic for `n < 3.18 * 10^23` is from the literature on strong pseudoprimes (Sorenson and Webster), from memory; my tests also confirm it on every number below 300,000, on 20,000 random numbers below 10^12, and on the known pseudoprimes listed in the text. The explanations and code are my own.
- The Project Euler problem (largest prime factor of 600851475143) and LeetCode numbers and titles are from memory; check a number before you rely on it.
