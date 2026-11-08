# DSA 11.03: Combinatorics, Probability and Sampling

*Part of the [Bits and math plan](00_plan.md) · Time: about 6 hours (3 reading, 3 problems) · You need: [Number theory and arithmetic](02_number_theory_and_arithmetic.md) (modular inverse and Fermat's little theorem, used for every modular binomial here), [Recursion fundamentals](../06_recursion_and_backtracking/01_recursion_fundamentals.md) (the recurrences for Pascal and Catalan), and C 01 (overflow, and why `rand() % n` is not uniform)*

Counting problems ask "how many?" and the answer is almost always too large to list, so you need a formula and a way to evaluate it without overflow. Probability and sampling problems ask "how likely?" and "how do I pick fairly?". This tutorial covers the pieces that appear in interviews and in systems code: binomial coefficients (exact, and modulo a prime), Catalan numbers, inclusion-exclusion, expected values, and the sampling algorithms (shuffling, reservoir sampling, weighted choice, the alias method, and building one random generator from another).

By the end you can:

- compute `C(n, k)` exactly without overflow, modulo a prime with `O(1)` queries after `O(n)` set-up, and modulo a small prime for huge `n` (Lucas' theorem);
- count with stars and bars, multinomials, Catalan numbers and inclusion-exclusion, and recognise which one a problem is;
- use linearity of expectation, and compute birthday and coupon-collector numbers;
- write Fisher-Yates and explain, with a count, why the "obvious" shuffle is biased;
- write reservoir sampling and prove it is uniform; pick with weights by prefix sums or by the alias method;
- turn `rand7()` into `rand10()`, and explain modulo bias and rejection sampling.

**Where the numbers come from.** The code below was compiled with `-Wall -Wextra` and tested: 1,103,443 checks passed under AddressSanitizer and UBSan (GCC 13.3). The tests ran: `choose_exact` against a 128-bit Pascal table for every `n < 80` (including the largest value that fits in 64 bits, `C(67, 33)`, and the first that does not); `pascal` against the factorial formula; `Binomial` against Pascal's triangle; Lucas' theorem against Pascal's triangle for the primes 2, 3, 5, 7, 13 and `n` up to 400; stars and bars against enumeration; Catalan numbers against the known values, against enumeration of bracket strings, and against a count of binary search trees; derangements against `next_permutation` for `n` up to 9; the expected number of fixed points (exactly 1) over every permutation for `n` up to 8; `count_coprime_upto` against a gcd loop for all `N, m <= 400`; the birthday and coupon-collector numbers against enumeration and the closed form; the shuffles by **exact enumeration of every possible index sequence** (so no randomness is involved in that check) and by 600,000 random trials; reservoir sampling against a 5-sigma band; weighted choice and the alias table by counting every outcome exactly; and `rand10` by an exact table of the 49 pairs and by 1,000,000 draws.

**Data and timing.** Inputs are synthetic and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are the **best of 3 repetitions, shown for run 1 / run 2**; they are this machine's, not yours. Counts and traces are the same in both runs.

---

## 1. Counting basics, and `C(n, k)` exactly

Two rules do most of the work. The **product rule**: if one choice has `a` options and a second, independent of the first, has `b`, there are `a * b` combined choices. The **sum rule**: if the cases do not overlap, add them.

From these come the standard counts. Arranging `n` distinct items in a row: `n!`. Choosing an **ordered** list of `k` out of `n`: `n * (n-1) * ... * (n-k+1)`. Choosing an **unordered** set of `k` out of `n` divides out the `k!` orders of each set:

```
C(n, k) = n! / (k! (n-k)!)          also written "n choose k"
```

The numbers have a famous recurrence. Take a particular item. Either the set contains it (choose the other `k-1` from the remaining `n-1`) or it does not (choose all `k` from the remaining `n-1`):

```
C(n, k) = C(n-1, k-1) + C(n-1, k),     C(n, 0) = C(n, n) = 1
```

That is **Pascal's triangle**. Rows 0 to 6, from the program (each row is `C(i, 0) ... C(i, i)`):

```
T1 Pascal rows 0..6: [1] [1 1] [1 2 1] [1 3 3 1] [1 4 6 4 1] [1 5 10 10 5 1] [1 6 15 20 15 6 1]
```

Two properties to remember: each row is symmetric (`C(n, k) = C(n, n-k)`), and row `n` sums to `2^n` (every subset has some size).

**Exact values.** `n!` overflows 64 bits at `n = 21`, so never compute the factorials. Multiply and divide step by step instead. After step `i` the running value is `C(n-k+i, i)`, which is an integer, so every division is exact:

```cpp
// n choose k exactly, as a 64-bit number, or false if the result does not fit.  Multiply by (n-k+i) then divide by i, for i = 1..k.
// After step i the running value is C(n-k+i, i), an integer, so every division is exact.  Precondition n < 2^32: the running value is
// below 2^64 when we multiply (we stop as soon as it is not), the factor is below 2^32, so the 128-bit product cannot overflow.
bool choose_exact(u64 n, u64 k, u64& out) {
    if (k > n) { out = 0; return true; }
    if (n >= (1ull << 32)) return false;             // outside the precondition
    k = std::min(k, n - k);                          // C(n, k) = C(n, n-k): fewer steps
    u128 r = 1;
    for (u64 i = 1; i <= k; ++i) {
        r = r * (n - k + i) / i;                     // exact division
        if (r > (u128)UINT64_MAX) return false;      // too large for u64 (the next steps only grow it)
    }
    out = (u64)r; return true;
}
// Pascal's triangle, rows 0..n, entries modulo m: C(i, j) = C(i-1, j-1) + C(i-1, j).  O(n^2) time, one row of memory if you only need row n.
std::vector<std::vector<u32>> pascal(int n, u32 m) {
    std::vector<std::vector<u32>> c(n + 1);
    for (int i = 0; i <= n; ++i) {
        c[i].assign(i + 1, 1 % m);
        for (int j = 1; j < i; ++j) c[i][j] = (c[i-1][j-1] + c[i-1][j]) % m;
    }
    return c;
}
```

Trace of `C(8, 3)`, the running values after each step:

```
T3 steps of C(8,3): r=6 21 56 (= C(6,1) C(7,2) C(8,3))
```

The values are `C(6,1)`, `C(7,2)`, `C(8,3)`. The intermediate product `r * (n-k+i)` is held in 128 bits so it cannot overflow; the loop stops as soon as the value no longer fits in 64 bits. The function takes `min(k, n-k)` to do fewer steps. The boundary, from the program:

```
T2 C(67,33)=14226520737620288370 fits=1  C(68,34) fits=0  C(52,5)=2598960  C(100,50) mod p=538992043
```

`C(67, 33)` is the largest central binomial that fits in an unsigned 64-bit number, and `C(68, 34)` does not. When the problem says "return the answer modulo `10^9 + 7`", this exact routine is not the one you want; the next two sections are.

**Pascal's triangle modulo `m`.** `pascal(n, m)` above builds all rows with the recurrence and reduces each entry. It needs only additions, so it works for **any** modulus, prime or not. Time and memory are `O(n^2)`; if you want only row `n`, keep two rows. Measured: `pascal(3000, 10^9 + 7)`, about 4.5 million entries, took 15.5 / 12.1 ms.

## 2. Many queries: factorials and inverse factorials

If `m` is a **prime `p` larger than `n`**, then `n!` is invertible modulo `p`, and `C(n, k) = n! * (k!)^-1 * ((n-k)!)^-1`. Precompute the factorials and their inverses once, and every query is two multiplications. The trick for the inverses is to compute only **one** inverse (Fermat's little theorem, [11.02](02_number_theory_and_arithmetic.md)) and derive the others, because `1/(i-1)! = i * (1/i!)`.

```cpp
// Many queries of C(n, k) mod a prime p > n: precompute factorials and inverse factorials once, then each query is 2 multiplications.
u64 power_mod(u64 b, u64 e, u64 m) { u64 r = 1 % m; b %= m; while (e) { if (e & 1) r = r * b % m; b = b * b % m; e >>= 1; } return r; }
struct Binomial {
    std::vector<u64> f, inv_f; u64 p;
    Binomial(int n, u64 prime = MOD) : f(n + 1), inv_f(n + 1), p(prime) {
        f[0] = 1; for (int i = 1; i <= n; ++i) f[i] = f[i-1] * i % p;
        inv_f[n] = power_mod(f[n], p - 2, p);                           // one modular inverse (Fermat) ...
        for (int i = n; i > 0; --i) inv_f[i-1] = inv_f[i] * i % p;      // ... then 1/(i-1)! = i / i!
    }
    u64 choose(int n, int k) const { return (k < 0 || k > n) ? 0 : f[n] * inv_f[k] % p * inv_f[n-k] % p; }
    u64 multinomial(int n, const std::vector<int>& parts) const {        // n! / (a1! a2! ... ak!), the parts must sum to n
        u64 r = f[n]; for (int a : parts) r = r * inv_f[a] % p; return r;
    }
};
// Lucas' theorem: for a SMALL prime p and huge n, k:  C(n, k) = product of C(n_i, k_i) over the base-p digits, all mod p.
u64 lucas(u64 n, u64 k, u32 p, const std::vector<std::vector<u32>>& small /* pascal(p-1, p) */) {
    u64 r = 1;
    while (n || k) {
        u32 ni = n % p, ki = k % p;
        if (ki > ni) return 0;
        r = r * small[ni][ki] % p;
        n /= p; k /= p;
    }
    return r % p;
}
// Stars and bars: the number of ways to write n as an ordered sum of k non-negative integers is C(n + k - 1, k - 1).
u64 stars_and_bars(int n, int k, const Binomial& B) { return B.choose(n + k - 1, k - 1); }
```

The three things that go wrong, and the checks:

- `p <= n`: then `n!` contains the factor `p`, is 0 modulo `p`, and has no inverse. That is exactly the case Lucas' theorem handles below.
- `k < 0` or `k > n`: the function returns 0, which is the right count.
- Overflow: every product is of two numbers below `p`, so `u64` is safe for `p < 2^32`.

A query, from the program:

```
T2 C(67,33)=14226520737620288370 fits=1  C(68,34) fits=0  C(52,5)=2598960  C(100,50) mod p=538992043
```

The last number there, `C(100, 50) mod p`, comes from this table. Measured: building the table for `n = 10^6` took 13.5 / 10.3 ms, and building it plus answering 1,000,000 queries took 26.2 / 21.2 ms, so a query costs a few nanoseconds.

**Multinomials.** The number of ways to arrange `n` items in a row when there are `a1` of one kind, `a2` of another, and so on (with `a1 + ... + ak = n`) is `n! / (a1! a2! ... ak!)`. This is the number of distinct anagrams of a word. `multinomial` above is it; `MISSISSIPPI` is in the problems.

**Stars and bars.** In how many ways can `n` identical items go into `k` labelled boxes (empty boxes allowed)? Lay the `n` items out as stars and place `k - 1` bars between them to separate the boxes. A layout is a choice of which `k - 1` of the `n + k - 1` positions hold bars, so the answer is `C(n + k - 1, k - 1)`. If every box must be non-empty, put one item in each box first and the answer is `C(n - 1, k - 1)`. From the program: the ways to write 5 as `x1 + x2 + x3` with `xi >= 0`, and a multinomial:

```
T4 stars and bars: x1+x2+x3=5 non-negative: 21 ways; multinomial(5;2,2,1)=30
```

(`C(7, 2) = 21`, and `5! / (2! 2! 1!) = 30`.)

**Lucas' theorem: a small prime and huge `n`.** Suppose the modulus is a **small** prime `p` (say 7) and `n`, `k` are as large as `10^18`. The factorials are useless since `n! = 0 mod p`. Lucas' theorem says: write `n` and `k` in base `p`; then

```
C(n, k) = product over the digits of C(n_i, k_i)    (mod p)
```

and any digit with `k_i > n_i` makes the whole thing 0 (because `C(a, b) = 0` for `b > a`). Each `C(n_i, k_i)` has both arguments below `p`, so a small Pascal table of size `p x p` answers it. The loop in `lucas` above peels one base-`p` digit per step, so a query costs `O(log_p n)`. From the program:

```
T5 Lucas C(100,50) mod 7=4, Pascal check mod 7 of C(100,50)=4; digits of 100 in base 7 = 2 0 2, of 50 = 1 0 1
```

100 in base 7 is `2 0 2` and 50 is `1 0 1`, so the answer is `C(2,1) * C(0,0) * C(2,1) = 2 * 1 * 2 = 4`, and the full Pascal table agrees. For a **composite** modulus, factor it into prime powers, solve each, and combine with the Chinese remainder theorem; that is an editor exercise, not interview material.

## 3. Catalan numbers

The sequence `1, 1, 2, 5, 14, 42, ...` appears in so many counting problems that recognising it is itself a skill. The formula and the recurrence:

```
C_n = C(2n, n) / (n + 1)          C_(n+1) = sum for i = 0..n of C_i * C_(n-i)
```

```cpp
// Catalan numbers 1, 1, 2, 5, 14, 42, ...:  C_n = C(2n, n) / (n + 1).  Recurrence: C_{n+1} = sum over i of C_i * C_{n-i}.
// They count balanced bracket strings with n pairs, binary trees with n nodes, ways to triangulate an (n+2)-gon, monotone lattice
// paths that stay on one side of the diagonal, and more.
u64 catalan_mod(int n, const Binomial& B) {
    return B.choose(2 * n, n) * power_mod(n + 1, B.p - 2, B.p) % B.p;
}
std::vector<u64> catalan_table(int n, u64 m) {              // by the recurrence: no division, any modulus
    std::vector<u64> c(n + 1, 0); c[0] = 1 % m;
    for (int i = 1; i <= n; ++i) for (int j = 0; j < i; ++j) c[i] = (c[i] + c[j] * c[i-1-j]) % m;
    return c;
}
```

The values `C_0` to `C_12`, from the program:

```
T6 Catalan 0..12: 1 1 2 5 14 42 132 429 1430 4862 16796 58786 208012
```

**Why the recurrence.** Take balanced bracket strings with `n + 1` pairs. The first `(` is closed by some `)`. What is inside that pair is a balanced string with `i` pairs, and what follows it is a balanced string with `n - i` pairs. The two parts are independent, so the count for that `i` is `C_i * C_(n-i)`; summing over `i` gives the recurrence. The same split works for every other thing Catalan counts:

- **balanced bracket strings** with `n` pairs (the five for `n = 3` are listed below);
- **binary trees with `n` nodes**, and binary search trees with `n` distinct keys (pick the root; the left and right subtrees are independent): LeetCode 96, "Unique Binary Search Trees";
- **triangulations** of a convex polygon with `n + 2` sides;
- **monotone paths** in an `n x n` grid that never cross the diagonal;
- ways to parenthesise a product of `n + 1` factors.

The five strings with 3 pairs, generated by the program (open a bracket if fewer than 3 are open; close one if that keeps the prefix balanced):

```
T7 balanced strings with 3 pairs: ((())) (()()) (())() ()(()) ()()()
```

The `C(2n, n) / (n + 1)` formula needs a division, so modulo a prime use `catalan_mod`; modulo any number, use the recurrence (`O(n^2)`).

## 4. Inclusion-exclusion

To count the elements in a union of sets, adding the sizes counts the overlaps twice; subtracting the pairwise overlaps removes too much; and so on, alternating:

```
|A1 u A2 u ... u An| = sum |Ai| - sum |Ai n Aj| + sum |Ai n Aj n Ak| - ...
```

It is the right tool when "at least one of these bad things" is hard to count but "all of these specific bad things at once" is a short formula. Two classic uses are in the code.

**Derangements.** A **derangement** is a permutation with no fixed point (nobody gets their own hat). Let `Ai` be the set of permutations that fix position `i`. Then `|Ai n Aj n ...|` over `j` chosen positions is `(n - j)!`, and inclusion-exclusion gives `D(n) = n! (1 - 1/1! + 1/2! - ...)`, which is `n!/e` rounded to the nearest integer. There is also a recurrence with a direct argument: element `n` goes to some other position `i` (`n - 1` choices); either `i`'s element goes to position `n` (a swap, leaving `D(n-2)`) or it does not (which behaves like `D(n-1)`); so `D(n) = (n - 1)(D(n-1) + D(n-2))`.

```cpp
// Inclusion-exclusion: |A1 u ... u An| = sum |Ai| - sum |Ai n Aj| + sum |Ai n Aj n Ak| - ...
// Derangements (permutations with no fixed point): D(n) = n! sum (-1)^i / i!, and the recurrence D(n) = (n-1)(D(n-1) + D(n-2)).
std::vector<u64> derangements(int n) {                      // exact in u64 up to n = 20
    std::vector<u64> d(n + 1, 0); d[0] = 1; if (n >= 1) d[1] = 0;
    for (int i = 2; i <= n; ++i) d[i] = (u64)(i - 1) * (d[i-1] + d[i-2]);
    return d;
}
// How many integers in 1..N are divisible by NONE of the given pairwise-coprime numbers (for example the distinct primes of m)?
// Include-exclude over the subsets: add N / product for an even-size subset, subtract it for an odd-size one.
long long count_not_divisible(long long N, const std::vector<long long>& a) {
    int k = (int)a.size(); long long total = 0;
    for (unsigned mask = 0; mask < (1u << k); ++mask) {
        long long prod = 1; bool over = false;
        for (int i = 0; i < k && !over; ++i) if (mask >> i & 1u) { if (prod > N / a[i]) over = true; else prod *= a[i]; }
        if (over) continue;                                  // product exceeds N: N / product = 0
        total += (__builtin_popcount(mask) & 1 ? -1 : 1) * (N / prod);
    }
    return total;
}
// Numbers in 1..N coprime to m: exclude multiples of each distinct prime of m.
long long count_coprime_upto(long long N, long long m) {
    std::vector<long long> pr;
    for (long long d = 2; d * d <= m; ++d) if (m % d == 0) { pr.push_back(d); while (m % d == 0) m /= d; }
    if (m > 1) pr.push_back(m);
    return count_not_divisible(N, pr);
}
```

The values `D(0)` to `D(10)`, and the check against `n!/e`:

```
T8 derangements 0..10: 1 0 1 2 9 44 265 1854 14833 133496 1334961   10!/e=1334960.92
```

**Counting numbers coprime to `m`.** How many integers in `1..N` are divisible by none of a set of pairwise coprime numbers? Let `Ai` be the multiples of `ai`. The intersection of a subset is the multiples of the product, which is `N / product`. Enumerate the `2^k` subsets and add or subtract by size parity. With `k` distinct primes this is `2^k` terms, and `k <= 9` for any `m < 2.3 * 10^8`, so it is fast. From the program:

```
T9 numbers 1..100 divisible by none of 2,3,5: 26;  coprime to 30 up to 30: 8 = phi(30)=8
```

(For `m = 30` and `N = 30` the count is `phi(30) = 8`, the Euler totient from [11.02](02_number_theory_and_arithmetic.md).)

## 5. Probability and expectation

A **probability** is a number between 0 and 1: the favourable outcomes over all equally likely outcomes. An **expected value** `E[X]` is the average of `X` over the outcomes, weighted by their probabilities.

**Linearity of expectation** is the most useful fact in this tutorial: `E[X + Y] = E[X] + E[Y]` **always**, even if `X` and `Y` depend on each other. The method: write the quantity as a sum of 0/1 indicators, find each indicator's probability, and add the probabilities. Example: shuffle `n` items uniformly; what is the expected number of fixed points? Position `i` is a fixed point with probability `1/n`, there are `n` positions, so the expected number is `n * (1/n) = 1`, for every `n`. The positions are not independent, and it does not matter. (The tests confirm it by averaging over every permutation for `n` up to 8.)

**A random walk to a state: set up an equation.** A fair die is rolled until a 6 appears; how many rolls on average? With `E` the answer, the first roll succeeds with probability 1/6 and otherwise you are back where you started after one wasted roll: `E = 1 + (5/6) E`, so `E = 6`. In general, an expected time is "one step plus the expected remaining time from where you land".

```cpp
// Probability and expectation, computed exactly.
// Linearity of expectation: E[X + Y] = E[X] + E[Y] ALWAYS, even when X and Y are dependent.  Write the count as a sum of 0/1 indicators.
// Birthday problem: the probability that n people among d equally likely days all differ is (d/d)((d-1)/d)...((d-n+1)/d).
double birthday_all_distinct(int n, double d) { double p = 1; for (int i = 0; i < n; ++i) p *= (d - i) / d; return p; }
int people_for_collision(double d, double target) {          // smallest n with P(some pair shares a day) >= target
    double p = 1; int n = 0; while (1 - p < target) { p *= (d - n) / d; ++n; } return n;
}
// Coupon collector: with n kinds, each draw uniform, the expected number of draws to see all is n (1 + 1/2 + ... + 1/n).
// After i kinds are held, the wait for a new one is geometric with success probability (n - i)/n, mean n/(n - i); add them up.
double coupon_expected(int n) { double e = 0; for (int i = 0; i < n; ++i) e += (double)n / (n - i); return e; }
// Expected value by a DP over states: e.g. a fair die rolled until a 6 appears: E = 1 + (5/6) E  =>  E = 6.
```

**The birthday problem.** With `d` equally likely days, the probability that `n` people all differ is `(d/d) ((d-1)/d) ... ((d-n+1)/d)`, and the chance of at least one match is one minus that. For 365 days the surprise is how small `n` is; the same arithmetic tells you how many random 32-bit hash values you can draw before a collision is likely. From the program:

```
T10 birthday: 23 people -> P(match)=0.5073, 57 people -> 0.9901, smallest n for 50%: 23, for 99%: 57; for d=2^32: n=77164 (sqrt(2 ln2 * 2^32)=77163)
```

So 23 people give a 50.7% chance of a shared birthday, and 57 give 99%. For `d = 2^32` the threshold is 77,164 values, close to the rule `n ~ sqrt(2 ln 2 * d)`. This is why a 32-bit hash table key, or a 64-bit id from a random generator, collides far sooner than "one in 2^32" suggests: **collisions arrive at about the square root of the space**.

**The coupon collector.** With `n` kinds, each draw uniform, how many draws to see every kind? When you hold `i` kinds, a new one arrives with probability `(n - i)/n`, so that wait averages `n/(n - i)` draws; add the waits for `i = 0 .. n-1` to get `n (1 + 1/2 + ... + 1/n)`, about `n ln n`. From the program:

```
T11 coupon collector: n=6 -> 14.70 draws, n=50 -> 224.96, n=100 -> 518.74, n=1000 -> 7485.5
```

Six faces of a die take about 14.7 rolls to see all; 1000 coupons take about 7,485. It also bounds how long random testing needs to cover `n` cases.

## 6. Sampling

### 6.1 Shuffling: Fisher-Yates and the biased shuffle

A shuffle is fair if each of the `n!` orderings has probability exactly `1/n!`. **Fisher-Yates**: go from the end; at position `i` swap with a uniformly chosen position in `[0, i]`. The number of equally likely random choices is `n * (n-1) * ... * 1 = n!`, and each choice sequence gives a different final order, so every order appears exactly once.

The tempting alternative picks the swap partner from all of `[0, n-1]` every time. Then there are `n^n` equally likely choice sequences, and `n^n` is not a multiple of `n!` for `n >= 3`, so the orderings cannot all have the same count.

```cpp
// Fisher-Yates shuffle: for i from n-1 down to 1, swap a[i] with a[j], j uniform in [0, i].  Each of the n! permutations has
// probability exactly 1/n!, because there are n * (n-1) * ... * 1 = n! equally likely index sequences and each gives a different result.
template <class RNG> void fisher_yates(std::vector<int>& a, RNG& g) {
    for (int i = (int)a.size() - 1; i > 0; --i) {
        std::uniform_int_distribution<int> d(0, i);
        std::swap(a[i], a[d(g)]);
    }
}
// The common WRONG shuffle: swap a[i] with a[j], j uniform in [0, n-1] every time.  n^n equally likely index sequences cannot be
// spread evenly over n! permutations when n^n is not a multiple of n!, so some permutations are more likely than others.
template <class RNG> void naive_shuffle(std::vector<int>& a, RNG& g) {
    std::uniform_int_distribution<int> d(0, (int)a.size() - 1);
    for (int i = 0; i < (int)a.size(); ++i) std::swap(a[i], a[d(g)]);
}
// Reservoir sampling (Algorithm R): keep k items from a stream of unknown length so each item ends up kept with probability k/n.
// Item number i (0-based) replaces a random slot if a draw j in [0, i] falls below k.  Proof: P(item i survives to the end) =
// k/(i+1) * product over t > i of (1 - 1/(t+1)) = k/(i+1) * (i+1)/n = k/n.
template <class RNG> std::vector<int> reservoir(const std::vector<int>& stream, int k, RNG& g) {
    std::vector<int> r;
    for (int i = 0; i < (int)stream.size(); ++i) {
        if (i < k) { r.push_back(stream[i]); continue; }
        std::uniform_int_distribution<int> d(0, i);
        int j = d(g); if (j < k) r[j] = stream[i];
    }
    return r;
}
// Weighted pick: build prefix sums of the weights, draw r uniform in [0, total), return the first index whose prefix exceeds r.  O(log n) per pick.
struct WeightedPicker {
    std::vector<long long> pre;
    explicit WeightedPicker(const std::vector<int>& w) { long long s = 0; for (int x : w) { s += x; pre.push_back(s); } }
    long long total() const { return pre.back(); }
    int pick_for(long long r) const { return (int)(std::upper_bound(pre.begin(), pre.end(), r) - pre.begin()); }   // r in [0, total)
};
// Alias method (Vose): O(n) setup, O(1) per sample.  Scale so each column has total weight W = sum of weights; n columns of height W
// (here every column has capacity W, and item i brings w_i * n).  Each column holds at most two items: itself (prob[c] of W) and an alias.
struct AliasTable {
    int n; long long W; std::vector<long long> prob; std::vector<int> alias;   // exact integer version: a draw is (column c, r in [0, W))
    explicit AliasTable(const std::vector<int>& w) : n((int)w.size()), W(0), prob(w.size()), alias(w.size(), 0) {
        for (int x : w) W += x;
        std::vector<long long> s(n); std::vector<int> small, large;
        for (int i = 0; i < n; ++i) { s[i] = (long long)w[i] * n; (s[i] < W ? small : large).push_back(i); }
        while (!small.empty() && !large.empty()) {
            int l = small.back(); small.pop_back(); int g = large.back(); large.pop_back();
            prob[l] = s[l]; alias[l] = g;
            s[g] -= (W - s[l]);                              // the big item donates the rest of l's column
            (s[g] < W ? small : large).push_back(g);
        }
        for (int i : large) { prob[i] = W; alias[i] = i; }   // what is left has exactly W units: a whole column of its own
    }
    int pick(int column, long long r) const { return r < prob[column] ? column : alias[column]; }   // r in [0, W)
};
// rand10 from rand7 (LeetCode 470): two rolls make a uniform number in 1..49; reject 41..49 and take the rest modulo 10.
template <class R7> int rand10(R7 rand7) {
    while (true) { int x = (rand7() - 1) * 7 + rand7(); if (x <= 40) return 1 + (x - 1) % 10; }
}
```

To check, the program **enumerates every one of the `n^n` choice sequences** for the biased shuffle and counts how often each ordering occurs (no random numbers involved):

```
T12 naive shuffle n=3: 27 sequences over 6 permutations, least likely 4, most likely 5 (ratio 1.25); Fisher-Yates: every permutation exactly once of n! sequences
T12 naive shuffle n=4: 256 sequences over 24 permutations, least likely 8, most likely 15 (ratio 1.88); Fisher-Yates: every permutation exactly once of n! sequences
T12 naive shuffle n=5: 3125 sequences over 120 permutations, least likely 16, most likely 47 (ratio 2.94); Fisher-Yates: every permutation exactly once of n! sequences
```

For three items, 27 sequences over 6 orderings cannot split evenly. The exact counts:

```
T13 naive shuffle of {0,1,2}, counts out of 27: 012:4 021:5 102:5 120:5 201:4 210:4
```

(`n = 2` is a curiosity: 4 sequences over 2 orderings is even, so the biased shuffle happens to be fair there; the tests assert that.) The bias grows with `n`: for 5 items the most likely ordering is almost three times as likely as the least likely. Fisher-Yates on three items, 600,000 shuffles:

```
T14 Fisher-Yates 3 items, 600000 shuffles: min 99718 max 100327 (expected 100000 each)
```

The counts are all within sampling noise of 100,000. Fisher-Yates is `O(n)` time and `O(1)` extra space, and its quality is limited by the random generator you give it.

### 6.2 Reservoir sampling

Task: keep `k` items, chosen uniformly, from a stream whose length you do not know and cannot store. **Algorithm R**: keep the first `k`; for item number `i` (0-based) draw `j` uniformly in `[0, i]`, and if `j < k` replace slot `j` with the item. The proof that every item ends up kept with probability `k/n`: item `i` enters with probability `k/(i+1)`, and it survives each later arrival `t` with probability `1 - 1/(t+1)`, so overall

```
P(item i is kept) = k/(i+1) * (i+1)/(i+2) * (i+2)/(i+3) * ... * (n-1)/n  =  k/n
```

(the product telescopes). It uses `O(k)` space, one pass, and one random number per item. From the program, 200,000 runs keeping 7 of 50 items; each item should be kept 28,000 times:

```
T15 reservoir k=7 of 50, 200000 runs: item kept min 27477 max 28367, expected 28000, 5-sigma band +-776
```

The least and most frequently kept items are well inside the 5-sigma band. This is LeetCode 382 and 398 with `k = 1`.

### 6.3 Weighted choice: prefix sums and the alias method

Pick item `i` with probability `w_i / W`, where `W` is the sum. The direct method: build the prefix sums, draw `r` uniform in `[0, W)`, and binary-search for the first prefix greater than `r` (`WeightedPicker` above, `O(n)` set-up, `O(log n)` per pick; this is LeetCode 528). With weights `{3, 1, 0, 4}` the prefix sums are `3 4 4 8`, and each `r` from 0 to 7 maps to an item (the weight-0 item is never chosen):

```
T16 weights {3,1,0,4}, prefix sums: 3 4 4 8; r=0..7 -> 0 0 0 1 3 3 3 3
```

The **alias method** gets `O(1)` per pick for `O(n)` set-up. Imagine `n` columns, each of height `W`, and spread the items so that each column holds **at most two** items: its own item and an "alias". A pick chooses a column uniformly and then a height `r` in `[0, W)`; if `r` is below the column's own share the column's item wins, otherwise the alias wins. Building it (Vose's method): scale each weight to `w_i * n`; items below `W` are "small", the others "large"; pair a small item with a large one, let the small item fill its column's lower part, and let the large one donate the rest of that column, which reduces the large item's remaining weight.

The version above uses integers, so the check is exact. For the weights `{3, 1, 0, 4}` (`W = 8`, scaled weights 12, 4, 0, 16):

```
T17 alias table weights {3,1,0,4}: W=8 | col 0: prob 8/8 alias 0 | col 1: prob 4/8 alias 3 | col 2: prob 0/8 alias 3 | col 3: prob 4/8 alias 0 | outcomes of 32: 12 4 0 16
```

Over all `4 * 8 = 32` equally likely (column, height) pairs, the outcomes are 12, 4, 0, 16: exactly `w_i * n`, so each item has exactly probability `w_i / W`. Measured with 1,000,000 weights in `1..100`: the set-up took 22.7 / 18.3 ms for the alias table and 4.8 / 3.8 ms for prefix sums; 10,000,000 picks took 243.6 / 238.6 ms with the alias table and 2318.9 / 2238.5 ms with prefix sums and binary search. (Generating the random numbers alone cost 45.0 / 48.4 ms in a loop I wrote for that, but the compiler may have simplified it, so treat that number loosely. I did not profile why the binary search is so much slower; a likely reason is cache misses over an 8 MB array, but I did not measure that.) For a few picks, prefix sums are enough; for millions, the alias table pays off.

### 6.4 `rand10()` from `rand7()`, rejection and modulo bias

Given a generator `rand7()` uniform on 1 to 7 (LeetCode 470), produce a uniform 1 to 10. Two calls make `x = (a - 1) * 7 + b`, uniform on 1 to 49. Forty of those values (1 to 40) split evenly into ten groups of four; **reject** 41 to 49 and try again. The counts over the 49 equally likely pairs, and the measured number of `rand7()` calls per `rand10()`:

```
T18 rand10 from rand7: of 49 pairs, 9 are rejected; counts for 1..10: 4 4 4 4 4 4 4 4 4 4; mean rand7 calls per rand10 = 2.450 (theory 98/40 = 2.45)
```

The mean is `2 * 49/40 = 2.45` calls (each attempt uses two and succeeds with probability 40/49).

**Modulo bias.** The tempting `rand() % n` is not uniform unless `n` divides the generator's range. A uniform byte (0 to 255) taken modulo 10:

```
T19 modulo bias: a uniform byte (0..255) taken mod 10 gives counts: 26 26 26 26 26 26 25 25 25 25  (digits 0..5 are 26/25 = 1.04x likelier)
```

Digits 0 to 5 get 26 byte values and 6 to 9 get 25, so they are about 4% more likely. The fix is the same rejection idea: throw away the top partial block (here 250 to 255) and redraw. `std::uniform_int_distribution` does this for you; `% n` on a 32-bit random number is only acceptably close when `n` is tiny compared with `2^32`.

**Rejection sampling in general.** To sample from a region you cannot sample directly, sample from a bigger region you can, and reject points outside. A uniform point in the unit circle: draw `(x, y)` in the square `[-1, 1]^2` and accept when `x^2 + y^2 <= 1`. The accepted fraction is the ratio of the areas, `pi/4`:

```
T20 uniform point in unit circle by rejection: accepted 0.7855 of squares (pi/4=0.7854)
```

The expected number of tries is the reciprocal of the acceptance rate, so a region that fills a small part of its bounding box wastes most draws.

## 7. Complexities

| Operation | Time | Space | Needs |
|---|---|---|---|
| `C(n, k)` exact | `O(min(k, n-k))` | `O(1)` | fits in 64 bits |
| Pascal's triangle, any modulus | `O(n^2)` | `O(n^2)`, or `O(n)` for one row | nothing |
| Factorial tables, `C(n, k)` mod prime | `O(n)` set-up, `O(1)` per query | `O(n)` | prime `p > n` |
| Lucas' theorem | `O(log_p n)` per query | `O(p^2)` | small prime `p` |
| Catalan, formula / recurrence | `O(1)` after tables / `O(n^2)` | | prime / any modulus |
| Inclusion-exclusion over `k` sets | `O(2^k * k)` | `O(1)` | small `k` |
| Fisher-Yates | `O(n)` | `O(1)` | a uniform generator |
| Reservoir sampling | `O(n)` | `O(k)` | one pass |
| Weighted pick, prefix sums | `O(n)` set-up, `O(log n)` per pick | `O(n)` | |
| Weighted pick, alias | `O(n)` set-up, `O(1)` per pick | `O(n)` | |

## 8. Which tool for which problem

- "Number of ways to choose / arrange": a binomial, a multinomial, or `n!`.
- "Number of ways to split `n` identical things into `k` groups": stars and bars.
- "Number of valid bracket strings / BSTs / triangulations": Catalan. If the answer for `n` is a sum of products of the answers for smaller sizes, suspect it.
- "Number with none of these properties": inclusion-exclusion, or the complement.
- "Expected number of ...": linearity of expectation with indicators.
- "Expected time until ...": one step plus the expected remaining time, and solve the equation.
- "Fair shuffle": Fisher-Yates. "Random sample of unknown-length input": reservoir. "Weighted random": prefix sums, or alias if there are many draws.
- "Build one generator from another": rejection.

## 9. Common mistakes

1. **Computing `n!` and dividing** (overflows at 21 for 64 bits). Multiply and divide step by step, or use factorial tables modulo a prime.
2. **Dividing in a modular formula** instead of multiplying by the inverse, or using Fermat's inverse with a composite modulus.
3. **`C(n, k)` modulo `p` with `p <= n`.** The factorial is 0; use Lucas' theorem.
4. **Forgetting `k > n` or negative `k`** (the count is 0).
5. **Computing `C(2n, n) / (n + 1)` with a division before the modulus**; use the inverse or the recurrence.
6. **Shuffling with a random partner from the whole range each time.** The result is biased (shown above by counting).
7. **`rand() % n`** for a non-power-of-two `n` (modulo bias).
8. **Seeding the generator badly** (for example `srand(time(0))` inside a loop, so every call reuses the same seed).
9. **Reservoir sampling with `j` drawn from `[0, k)` instead of `[0, i]`.**
10. **Counting `D(n)` as `n! / e` in floating point** for large `n`; use the recurrence with integers.
11. **Believing "independent" is needed for the expectation of a sum.** It is not (that is the point of linearity); it is needed for a product and for the variance.
12. **Assuming the birthday threshold is `d / 2`.** It is about `sqrt(d)`.

## 10. Questions to answer out loud

1. Derive `C(n, k) = C(n-1, k-1) + C(n-1, k)` in words.
2. How do you compute `C(n, k)` exactly without overflowing on the factorials? Why is every division exact?
3. How do you answer a million `C(n, k) mod p` queries? Why only one modular inverse in the set-up?
4. When does that fail, and what do you use instead? State Lucas' theorem.
5. State stars and bars, with and without the non-empty condition.
6. Give four things counted by Catalan numbers and prove the recurrence for one of them.
7. Count derangements two ways.
8. State linearity of expectation. What is the expected number of fixed points of a random permutation, and why does independence not matter?
9. Why do collisions appear at about the square root of the space?
10. Write Fisher-Yates. Why is the swap-with-anyone version biased? Show it for `n = 3`.
11. Prove that reservoir sampling is uniform.
12. Pick a weighted item in `O(log n)`, then in `O(1)`. What is the trade-off?
13. Build `rand10()` from `rand7()`. What is the expected number of calls?
14. What is modulo bias and how do you remove it?

## 11. Problems

### 11.1 On paper

Answer first, then run the code to check.

**A.** What are `C(10, 3)` and `C(20, 10)`?

**B.** A robot moves right or down on a grid from the top-left to the bottom-right of a 4 by 3 grid of cells (3 moves right, 4 moves down in total): how many paths? (Choose which 3 of the 7 moves are "right".)

**C.** How many solutions does `x1 + x2 + x3 + x4 = 10` have in non-negative integers? In positive integers?

**D.** What is `C_5`? How many binary search trees hold the keys 1 to 4?

**E.** How many derangements are there of 5 items, and what is the probability that a random permutation of 5 is one?

**F.** How many integers in `1..1000` are divisible by none of 3, 5, 7?

**G.** Use Lucas' theorem to compute `C(7, 3) mod 3` and `C(10, 5) mod 3`.

**H.** With 365 equally likely birthdays, what is the smallest group where a shared birthday is at least 50% likely?

**I.** How many rolls of a fair die, on average, to see every face?

**J.** For the biased shuffle of `{0, 1, 2}` (swap each position with a random position of the three), in how many of the 27 choice sequences does the array end up unchanged as `0 1 2`?

**K.** How many distinct arrangements of the letters of `MISSISSIPPI` are there?

**L.** What is the expected number of `rand7()` calls per `rand10()` with the method above?

### 11.2 On LeetCode

Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | Where | How to approach |
|---|---|---|---|
| Unique Paths | 62 | LeetCode | `C(m + n - 2, m - 1)`, or the grid DP; compute the binomial step by step |
| Pascal's Triangle (I and II) | 118, 119 | LeetCode | The recurrence; for row `k` only, one array updated from the right |
| Unique Binary Search Trees | 96 | LeetCode | Catalan: root `i`, left `i - 1` keys, right `n - i` keys |
| Generate Parentheses | 22 | LeetCode | Backtracking (open if fewer than `n` open, close if that keeps the prefix balanced); the count is Catalan |
| Count Sorted Vowel Strings | 1641 | LeetCode | Stars and bars: `C(n + 4, 4)` |
| Shuffle an Array | 384 | LeetCode | Fisher-Yates on a copy; keep the original for `reset` |
| Linked List Random Node | 382 | LeetCode | Reservoir sampling with `k = 1` |
| Random Pick Index | 398 | LeetCode | Reservoir sampling over the positions of the target |
| Random Pick with Weight | 528 | LeetCode | Prefix sums and binary search |
| Implement Rand10() Using Rand7() | 470 | LeetCode | Rejection from `(a - 1) * 7 + b`; accept 1 to 40 |
| Number of Ways to Reorder Array to Get Same BST | 1569 | LeetCode | Recurse on left and right; combine with `C(left + right, left)` modulo `10^9 + 7` |

### 11.3 In your editor

**A.** Run the biased shuffle and Fisher-Yates a million times on 4 items and print the count of each of the 24 orderings. Compare with the exact counts (8 to 15 of 256 for the biased one).

**B.** Write Sattolo's algorithm (Fisher-Yates with `j` in `[0, i - 1]`) and check that it only ever produces permutations that are a single cycle. How many are there for `n = 5`?

**C.** Shuffle by assigning each item a random 64-bit key and sorting by the key. Compare its speed with Fisher-Yates, and say when the two could differ in fairness.

**D.** Estimate the birthday threshold for 32-bit values by simulation (insert random 32-bit numbers into a set until a repeat) and compare with 77,164.

**E.** Compute `C(n, k) mod 1000` for `n <= 2000` with `pascal(n, 1000)`. Then compute it modulo 8 and modulo 125 separately with `pascal` and combine the two answers with the Chinese remainder theorem from [11.02](02_number_theory_and_arithmetic.md); compare.

### Answers

The program that checks the paper problems printed:

```
A 120 184756
B 35
C 286 84
D 42 14
E 44 0.3667
F 457
G 2 0
H 23
I 14.70
J 4
K 34650
L 2.45
```

Reading it line by line:

**A.** `C(10, 3) = 10*9*8 / 3! = 120` and `C(20, 10) = 184,756`.

**B.** `C(7, 3) = 35`.

**C.** Non-negative: `C(10 + 4 - 1, 3) = C(13, 3) = 286`. Positive: subtract 1 from each, leaving 6 to place, `C(6 + 3, 3) = C(9, 3) = 84`.

**D.** `C_5 = 42` and the BST count for 4 keys is `C_4 = 14`.

**E.** `D(5) = 44` (`4 * (9 + 2)`), and the probability is `44/120 = 0.3667`, already close to `1/e = 0.3679`.

**F.** `1000 - (333 + 200 + 142) + (66 + 47 + 28) - 9 = 457`.

**G.** `7 = 2 1` and `3 = 1 0` in base 3, so `C(2, 1) * C(1, 0) = 2` (and `C(7, 3) = 35 = 2 mod 3`). `10 = 1 0 1` and `5 = 0 1 2` in base 3: in the lowest place `k_i = 2 > n_i = 1`, so the answer is 0 (and `C(10, 5) = 252 = 0 mod 3`).

**H.** 23.

**I.** `6 * (1 + 1/2 + 1/3 + 1/4 + 1/5 + 1/6) = 14.7`.

**J.** 4 of 27 sequences. The six orderings have counts 4 or 5, so `0 1 2` is slightly less likely than the 5-count orderings.

**K.** `11! / (1! 4! 4! 2!) = 34,650` (one M, four I, four S, two P).

**L.** `2 * 49 / 40 = 2.45`.

## Cheat sheet

- **Binomial:** `C(n, k) = C(n-1, k-1) + C(n-1, k)`; exact: multiply then divide step by step; mod a prime `p > n`: factorial and inverse-factorial tables, one inverse; small prime, huge `n`: Lucas by base-`p` digits.
- **Stars and bars:** `C(n + k - 1, k - 1)` (empty allowed), `C(n - 1, k - 1)` (non-empty).
- **Multinomial:** `n! / (a1! ... ak!)`.
- **Catalan:** `C(2n, n) / (n + 1)`, or `C_(n+1) = sum C_i C_(n-i)`: brackets, BSTs, triangulations.
- **Inclusion-exclusion:** alternate the sums over intersections; `D(n) = (n-1)(D(n-1) + D(n-2))`.
- **Expectation:** sum of indicators; no independence needed; expected fixed points of a permutation: 1.
- **Birthday:** collisions at about `sqrt(d)`. **Coupon collector:** `n H_n`, about `n ln n`.
- **Shuffle:** Fisher-Yates from the end, partner in `[0, i]`. **Reservoir:** replace slot `j` if `j < k`, `j` in `[0, i]`.
- **Weighted:** prefix sums and binary search; alias for `O(1)`.
- **`rand10` from `rand7`:** `(a - 1) * 7 + b`, reject above 40; 2.45 calls on average.
- **Modulo bias:** `% n` favours the small residues unless `n` divides the range; reject the partial top block.

---

## Sources

- All code is tested here (1,103,443 checks, GCC 13.3, sanitizers on); counts, traces and timings are measured on one Xeon container, two runs each. Times are this machine's.
- Pascal's triangle, Lucas' theorem, stars and bars, Catalan numbers, inclusion-exclusion, linearity of expectation, the birthday problem and the coupon collector are standard; see CLRS (appendix on counting and probability) and Knuth, *The Art of Computer Programming* volume 2 (random numbers and shuffling) and volume 4. Fisher-Yates and Algorithm R (reservoir sampling) are in Knuth volume 2. The alias method is due to Walker, with the linear-time construction by Vose; the integer version here is my own adaptation, tested by counting every outcome. The explanations and code are my own.
- LeetCode numbers and titles are from memory; check a number before you rely on it.
