# DSA 12.02: Rolling Hash and Rabin-Karp

*Part of the [String algorithms plan](00_plan.md) · Time: about 6 hours (3 reading, 3 problems) · You need: [KMP and the Z-function](01_kmp_and_the_z_function.md) (the deterministic way to search, which this tutorial's method is compared with), [Hash tables inside](../02_hashing/01_hash_tables_inside.md) and [Hashing patterns](../02_hashing/02_hashing_patterns.md) (what a hash function promises), [Number theory and arithmetic](../11_bits_and_math/02_number_theory_and_arithmetic.md) (modular arithmetic and 128-bit products)*

Comparing two strings of length `L` takes `O(L)`. If you could compare **any two substrings in `O(1)`**, a whole family of problems would collapse: repeated substrings, longest common substring, palindrome queries, pattern search. A **polynomial rolling hash** does exactly that, at the price of being *probabilistic*: it can say "equal" for two different strings, with a probability you can make astronomically small. This tutorial builds the hash, shows how to choose its parameters, proves what the collision probability is, demonstrates the standard ways it gets broken, and applies it to Rabin-Karp search, longest duplicate substring, longest common substring and palindrome queries.

By the end you can:

- define the polynomial hash of a substring and compute it in `O(1)` from prefix hashes;
- write Rabin-Karp search with a rolling update, with and without verification, and say what each variant guarantees;
- state the collision probability for a random base and a prime modulus, and use the birthday estimate for many strings;
- pick a modulus and base that cannot be attacked, and explain why `mod 2^64` with plain overflow is broken for every base;
- solve longest duplicate substring and longest common substring by binary search on the length plus hashing, in `O(n log n)`;
- answer "is `s[l..r)` a palindrome?" in `O(1)` after `O(n)` set-up.

**Where the numbers come from.** The code below was compiled with `-Wall -Wextra` and tested: 10,146,872 checks passed under AddressSanitizer and UBSan (GCC 13.3). The tests ran: the modular arithmetic against the `%` operator on 500,000 random pairs including values next to the modulus; the substring hash against **actual string equality for every pair of substrings** of 300 random short strings (alphabets of 1 to 3 letters, so equal substrings are common); hashes of different strings under one base; `lcp` against a direct loop; Rabin-Karp, verified and unverified, against the naive search on 30,000 random pairs; longest duplicate substring against a try-every-length brute force on 3,000 strings; longest common substring against the `O(nm)` table on 3,000 pairs; palindrome queries and distinct-substring counts against `std::set<string>` for every substring of 1,500 strings; the collision theory against a small prime (for 300 pairs of strings of length up to 19, never more than `length - 1` colliding bases out of 10,007); and the two attacks on the `mod 2^64` hash. I also broke the code on purpose in four ways to check that the tests notice. Two were caught (a shifted power in the substring hash, and a rolling update that forgets to remove the leaving character). Two were not: removing the verification step of `find_duplicate` (a collision modulo `2^61 - 1` is so unlikely that no test can see it, which is the point of the modulus) and removing a second reduction step in `mul61` (it turned out to be dead code, and the tutorial's code no longer has it).

**Data and timing.** Inputs are synthetic and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are the **best of 3 repetitions, shown for run 1 / run 2**; they are this machine's, not yours. Counts and traces are the same in both runs.

---

## 1. The idea

Treat a string as a number written in some base `B`. The string `s[l..r)` becomes

```
H(s[l..r)) = s[l] * B^(r-l-1) + s[l+1] * B^(r-l-2) + ... + s[r-1]      (mod M)
```

for a prime modulus `M`. This is exactly how you read a decimal number from its digits (`B = 10`). Two properties make it useful:

1. **Equal strings have equal hashes** (always).
2. **Different strings usually have different hashes**, with a failure probability that depends on `M` and on the choice of `B`, which we will compute.

Property 2 is why every use of a hash for equality is *probabilistic*. When two hashes differ, the strings are certainly different. When they agree, the strings are *very probably* the same, and you may confirm by comparing the characters.

## 2. Substring hashes in O(1)

Let `h[i]` be the hash of the prefix `s[0..i)`. Then `h[0] = 0` and `h[i+1] = h[i] * B + s[i]`. The hash of any substring follows by subtracting the shifted prefix:

```
H(s[l..r)) = h[r] - h[l] * B^(r-l)      (mod M)
```

because `h[r]` is `h[l]` shifted left by `r - l` places plus the hash of the stretch we want. The shift by `B^(r-l)` needs the powers of `B`, stored once.

**Arithmetic modulo `2^61 - 1`.** The modulus should be large (so collisions are rare) and the multiplication fast. A 61-bit Mersenne prime does both: a product of two residues fits in 122 bits, and because `2^61 = 1 (mod M)`, you reduce it by **adding its high bits to its low bits**, with no division.

```cpp
// Arithmetic modulo the Mersenne prime M = 2^61 - 1.  Because 2^61 = 1 (mod M), a product of two residues is reduced by adding its
// low 61 bits to its high bits, with no division.  This is as fast as a 64-bit hash and has a modulus near 2.3 * 10^18.
static const u64 M61 = (1ULL << 61) - 1;
inline u64 add61(u64 a, u64 b) { u64 r = a + b; return r >= M61 ? r - M61 : r; }
inline u64 sub61(u64 a, u64 b) { return a >= b ? a - b : a + M61 - b; }
inline u64 mul61(u64 a, u64 b) {
    u128 t = (u128)a * b;
    u64 r = (u64)(t & M61) + (u64)(t >> 61);          // at most 2M - 3 when a, b < M, so one subtraction is enough
    return r >= M61 ? r - M61 : r;
}
// A random base in [256, M - 1], different on every run.  A fixed base can be attacked by someone who knows it; a random one cannot.
inline u64 random_base() {
    std::mt19937_64 g((u64)std::chrono::steady_clock::now().time_since_epoch().count() ^ (u64)std::random_device{}());
    return 256 + g() % (M61 - 257);
}
// The value of a character in the polynomial: +1 so that the character '\0' does not look like "nothing".
inline u64 val(char c) { return (u64)(unsigned char)c + 1; }
```

The character's value is `c + 1`, not `c`, so that the character `'\0'` does not behave like "nothing" (otherwise `"a"` and `"\0a"` would hash alike).

```cpp
// Polynomial hash of s[l..r):  H = s[l]*B^(r-l-1) + s[l+1]*B^(r-l-2) + ... + s[r-1]   (mod M).
// With prefix hashes h[i] = hash of s[0..i), the hash of ANY substring is  h[r] - h[l]*B^(r-l)  in O(1), after O(n) set-up.
struct Hasher {
    vector<u64> h, pw; u64 B;
    Hasher(const string& s, u64 base) : h(s.size() + 1, 0), pw(s.size() + 1, 1), B(base) {
        for (size_t i = 0; i < s.size(); ++i) {
            h[i + 1] = add61(mul61(h[i], B), val(s[i]));
            pw[i + 1] = mul61(pw[i], B);
        }
    }
    u64 get(int l, int r) const { return sub61(h[r], mul61(h[l], pw[r - l])); }          // hash of s[l..r)
};
// Longest common prefix of the suffixes a[i..] and b[j..], by binary search on the length: O(log n) hash comparisons.
// Both hashers must have been built with the same base.
inline int lcp(const Hasher& A, int na, int i, const Hasher& Bh, int nb, int j) {
    int lo = 0, hi = std::min(na - i, nb - j);
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (A.get(i, i + mid) == Bh.get(j, j + mid)) lo = mid; else hi = mid - 1;
    }
    return lo;
}
```

A tiny check, with base 131 (so the characters `a`, `b`, `c` are 98, 99, 100) and the substring `bc`, computed two ways:

```
T1 base 131, characters a b c have values 98 99 100: prefix hashes h[0..3] = 0 98 12937 1694847; hash of "bc" as get(1,3) = h[3] - h[1]*131^2 = 13069, directly 99*131 + 100 = 13069
```

The 13069 from `h[3] - h[1] * 131^2` equals `99 * 131 + 100`, the hash of `bc` computed directly. The tests check this against real string equality for every pair of substrings of hundreds of short strings.

**Longest common prefix of two suffixes.** With `O(1)` substring hashes you can binary search the length of the common prefix: if the first `mid` characters agree then try longer, else shorter. That is `O(log n)` hash comparisons (the function `lcp` above). For `abcabcabd`, the suffixes starting at 0 and at 3 (`abcabcabd` and `abcabd`):

```
T9 longest common prefix of the suffixes of abcabcabd starting at 0 and at 3 (abcabcabd and abcabd): 5
```

They share `abcab`, five characters.

## 3. Rabin-Karp search

To find a pattern of length `m` in a text, compute the pattern's hash, then slide a window of length `m` along the text, **updating its hash in O(1)** instead of recomputing it: remove the leaving character, shift, add the entering one:

```
new = (old - t[i] * B^(m-1)) * B + t[i+m]
```

With a base of 131, the text `abcab`, and the pattern `cab`:

```
T2 Rabin-Karp, text abcab, pattern cab, base 131: pattern hash 1729037; window hashes abc = 1694847, bca = 1712137, cab = 1729037; rolling update from abc: (1694847 - 98*131^2) * 131 + 98 = 1712137
```

The last number is the hash of `bca`, obtained from the hash of `abc` without looking at the whole window again.

```cpp
// Rabin-Karp: slide a window of length m along the text, updating its hash in O(1):
//    new = (old - t[i]*B^(m-1)) * B + t[i+m]
// and compare with the pattern's hash.  With verify = true every hash match is confirmed by comparing the characters, so the answer is
// always right (a Las Vegas algorithm: only the running time is random); with verify = false a hash collision would report a false
// match (a Monte Carlo algorithm).  Returns all start positions, overlaps included.
inline vector<int> rabin_karp_find_all(const string& t, const string& p, u64 B, bool verify = true) {
    int n = (int)t.size(), m = (int)p.size(); vector<int> r;
    if (m == 0 || m > n) return r;
    u64 top = 1; for (int i = 1; i < m; ++i) top = mul61(top, B);                   // B^(m-1)
    u64 hp = 0, hw = 0;
    for (int i = 0; i < m; ++i) { hp = add61(mul61(hp, B), val(p[i])); hw = add61(mul61(hw, B), val(t[i])); }
    for (int i = 0; ; ++i) {
        if (hw == hp && (!verify || t.compare(i, m, p) == 0)) r.push_back(i);
        if (i + m >= n) break;
        hw = add61(mul61(sub61(hw, mul61(val(t[i]), top)), B), val(t[i + m]));
    }
    return r;
}
```

Two flavours, and the difference is a classic interview point:

- **With verification** (`verify = true`): when the hashes agree, compare the actual characters. The answer is always correct; only the running time is random (an occasional extra comparison). This is a **Las Vegas** algorithm.
- **Without verification**: trust the hash. The running time is fixed, but with a tiny probability the answer contains a false match. This is a **Monte Carlo** algorithm.

```
T3 aba in abababa: verified search 0 2 4; unverified search gives the same positions
```

**Cost.** Expected `O(n + m)`, but verification can hurt in a bad case: a text of all `a` and a pattern of `a`'s matches in every window, and each match is confirmed in `O(m)`. That is not a hash problem: there really are `n - m + 1` matches to confirm. If you only need to know *whether* there is a match, stop at the first.

**The practical question** is whether Rabin-Karp beats the library. It does not: see section 6. Its strength is **extensions**: the same rolling window works for *many patterns of the same length at once* (put their hashes in a set), for 2D patterns, and for any problem where "compare substrings" is the bottleneck.

## 4. Collisions: probability, birthdays and attacks

### 4.1 What is the chance that two different strings collide?

Take two different strings of length at most `L`. Their hashes are equal exactly when the polynomial `D(x) = (difference of the two hash polynomials)` is zero at the chosen base `x = B`. `D` is not the zero polynomial (the strings differ) and has degree less than `L`, and **a non-zero polynomial of degree `d` has at most `d` roots modulo a prime**. So at most `L - 1` of the `M` possible bases make the strings collide, and for a **base chosen uniformly at random**:

```
P(collision) <= (L - 1) / M
```

This holds for *every* pair of strings, however they were chosen, as long as the base is random and kept secret from whoever chose the strings. For `M = 2^61 - 1` and strings of length one million, the bound is about `4.3 * 10^-13` per comparison.

The bound can be reached. Modulo the small prime 10007, take `b` followed by 5003 letters `a`, and 5003 letters `a` followed by `b`. Their difference polynomial is a multiple of `x^5003 - 1`, which has many roots:

```
T5 two different strings of length 5004 (b then 5003 a's, and 5003 a's then b), modulo 10007: they have equal hashes for 5003 of the 10006 non-zero bases, close to the bound of length - 1 = 5003
```

Half of all bases fail, so the bound `L / M` is real, and the modulus must be much larger than the lengths of the strings.

### 4.2 Many comparisons: the birthday effect

With one comparison the risk is `L / M`. With many strings it is the **birthday problem** ([11.03](../11_bits_and_math/03_combinatorics_probability_and_sampling.md)): among `n` strings there are about `n^2 / 2` pairs, so the expected number of colliding pairs is about `n^2 / (2M)`. The program hashed 200,000 distinct random strings of 12 letters:

```
T4 200000 distinct random strings of 12 letters: pairs with equal hash: modulo 10^9+7 -> 21 (the birthday estimate n^2/(2M) is 20.0); modulo 2^61-1 -> 0 (estimate 8.7e-09)
```

With the common modulus `10^9 + 7`, 21 pairs collide (the estimate says 20): a single hash modulo `10^9 + 7` is **not safe for 200,000 strings**, let alone a million. With `2^61 - 1`, the estimate is around `10^-8` colliding pairs. That is why this tutorial uses a 61-bit modulus. (A common alternative is **double hashing**: two independent bases modulo two different primes near `10^9`, giving about `10^18` combined; it costs two multiplications per step instead of one 128-bit multiplication.)

### 4.3 Choosing the base, and the hash that is broken for every base

Choose the base **at run time, at random** (`random_base`). A fixed base can be reverse-engineered by anyone who knows it, and strings crafted against it collide every time; this is a standard way to break hash solutions in programming contests.

The other trap is to skip the modulus and let the 64-bit integer overflow, which is the same as working modulo `2^64`. It is fast and it is **broken for every base**, by the **Thue-Morse strings**. Build the string whose `i`-th letter is the parity of the number of 1 bits in `i`, and its letter-swapped twin. In my runs, for lengths of 1024 or more, the two strings collided for *every odd base I tried*. For an **even** base it is worse: `B^64 = 0 (mod 2^64)`, so only the last 64 characters influence the hash and any two strings with the same last 64 characters collide. Measured, with 1000 random odd bases per length:

```
T6 hash mod 2^64 (plain unsigned overflow) of the Thue-Morse string of length 2^k and its letter-swapped twin, over 1000 random odd bases, how many collide: [k=5 length 32: 0] [k=6 length 64: 4] [k=7 length 128: 33] [k=8 length 256: 111] [k=9 length 512: 273] [k=10 length 1024: 1000] [k=11 length 2048: 1000]; with an EVEN base, strings that differ only in their first character (length 100) collide in 1000 of 1000; the 2^61-1 hash on the length-1024 pair collides in 0 of 1000
```

(The twin pair at length 1024 never collides under the `2^61 - 1` hash, 0 of 1000.) The rule: **never hash modulo `2^64`**, whether by `unsigned` overflow or by `unordered_map` with a home-made polynomial hash over `unsigned long long`; and never use a fixed base when the input may be chosen by someone else.

## 5. Applications

### 5.1 Longest duplicate substring

Find the longest substring that occurs at least twice (copies may overlap). Key fact: **if a duplicate of length `L` exists, so does one of length `L - 1`** (drop the last character of both copies). So "is there a duplicate of length `L`?" is monotone in `L` and we can **binary search on the length**. To test one `L`, hash every window of length `L`, sort the (hash, start) pairs, and look for neighbours with equal hashes, confirming by comparing the characters.

```cpp
// Is there a substring of length L that occurs at least twice in s (the two copies may overlap)?  Hash every window, sort the
// (hash, start) pairs, and look at neighbours with equal hashes, confirming by comparing the characters.  Returns a start or -1.
inline int find_duplicate(const string& s, const Hasher& H, int L) {
    int n = (int)s.size(); vector<std::pair<u64, int>> v;
    for (int i = 0; i + L <= n; ++i) v.push_back({H.get(i, i + L), i});
    std::sort(v.begin(), v.end());
    for (size_t k = 0; k + 1 < v.size(); ++k)
        if (v[k].first == v[k + 1].first && s.compare(v[k].second, L, s, v[k + 1].second, L) == 0) return v[k].second;
    return -1;
}
// Longest duplicate substring (LeetCode 1044).  If a duplicate of length L exists then one of length L - 1 does (drop the last
// character of both), so the answer to "is there one of length L?" is monotone and we can binary search on L: O(n log n) overall.
inline string longest_duplicate_substring(const string& s, u64 base) {
    Hasher H(s, base); int lo = 1, hi = (int)s.size() - 1; string best;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, at = find_duplicate(s, H, mid);
        if (at >= 0) { best = s.substr(at, mid); lo = mid + 1; } else hi = mid - 1;
    }
    return best;
}
// Longest common substring of a and b: the same binary search; a length L works if some window of a equals some window of b.
inline string longest_common_substring(const string& a, const string& b, u64 base) {
    Hasher HA(a, base), HB(b, base); int lo = 1, hi = (int)std::min(a.size(), b.size()); string best;
    while (lo <= hi) {
        int L = (lo + hi) / 2; vector<std::pair<u64, int>> v;
        for (int i = 0; i + L <= (int)a.size(); ++i) v.push_back({HA.get(i, i + L), i});
        std::sort(v.begin(), v.end()); int found = -1;
        for (int j = 0; j + L <= (int)b.size() && found < 0; ++j) {
            u64 hv = HB.get(j, j + L);
            for (auto it = std::lower_bound(v.begin(), v.end(), std::make_pair(hv, -1)); it != v.end() && it->first == hv; ++it)
                if (a.compare(it->second, L, b, j, L) == 0) { found = j; break; }
        }
        if (found >= 0) { best = b.substr(found, L); lo = L + 1; } else hi = L - 1;
    }
    return best;
}
```

The search on `banana`, trying lengths 3, 4 and 5, and the answers for `banana` and `abcd`:

```
T7 longest duplicate substring of banana by binary search on the length (L = 3: ana at 1; L = 4: none; L = 5: none); answer "ana"; of abcd: ""
```

Cost: `O(n log n)` per test of one length (the sort) times `O(log n)` lengths, `O(n log^2 n)` with the sort; an `unordered_set` of hashes gives `O(n log n)` expected. For a random binary string of length 200,000 the longest duplicate is short, and a string of one repeated letter finishes quickly:

```
T12 longest duplicate substring of a random 0/1 string of 200000 letters has length 35
```

Measured: the random binary string of 200,000 letters took 306.3 / 233.5 ms, and 100,000 copies of the letter `a` took 3.0 / 3.4 ms. The deterministic alternative is a suffix array (12.06).

### 5.2 Longest common substring of two strings

The same binary search on the length: a length `L` works if some window of `a` equals some window of `b`. The function is in the same code block above.

```
T8 longest common substring of xabcdy and zzabcdw: abcd
T13 longest common substring of two random 0/1 strings of 100000 letters has length 34
```

The second line is the longest common substring of two random 0/1 strings of 100,000 letters, which took 325.9 / 309.2 ms.

### 5.3 Palindrome queries in O(1)

A stretch `s[l..r)` is a palindrome when it equals its own reverse. The reverse of `s[l..r)` is the stretch `rev[n-r .. n-l)` of the reversed string, so compare two hashes, one from a hasher on `s` and one from a hasher on `reverse(s)`. This answers any number of palindrome queries in `O(1)` each (and a binary search on the hash comparison finds the longest palindrome around a centre, an alternative to Manacher's algorithm in 12.03).

```cpp
// Is s[l..r) a palindrome?  It is when its hash equals the hash of the same stretch read backwards.  Build one hasher for s and one for
// reverse(s); the stretch s[l..r) reversed is rev[n-r .. n-l).  O(1) per query after O(n) set-up.
struct PalindromeQuery {
    int n; Hasher f, g;
    PalindromeQuery(const string& s, u64 base) : n((int)s.size()), f(s, base), g(string(s.rbegin(), s.rend()), base) {}
    bool is_palindrome(int l, int r) const { return f.get(l, r) == g.get(n - r, n - l); }
};
// Number of distinct substrings of length L.
inline int count_distinct(const string& s, const Hasher& H, int L) {
    vector<u64> v; for (int i = 0; i + L <= (int)s.size(); ++i) v.push_back(H.get(i, i + L));
    std::sort(v.begin(), v.end()); return (int)(std::unique(v.begin(), v.end()) - v.begin());
}
```

```
T10 palindrome queries on abacabad: [0,7) abacaba -> 1; [1,4) bac -> 0; [0,8) whole string -> 0; [2,5) aca -> 1
T11 distinct substrings of length 3 in abababab: 2 (aba and bab)
```

(The last line counts the distinct substrings of one length by sorting their hashes.)

## 6. Measured costs

Machine and method as above, text of 10,000,000 random letters over four symbols.

| Operation | Time, ms (run 1 / run 2) |
|---|---|
| Build the prefix hashes (`Hasher`) | 106.3 / 119.1 |
| 10,000,000 `get(l, l + 1000)` queries at random positions | 297.6 / 221.1 |
| The same loop of random numbers with no hashing | 36.7 / 26.2 |
| Rabin-Karp, pattern of 40 letters (verified) | 54.8 / 43.9 |
| Rabin-Karp, pattern of 1000 letters (verified) | 52.9 / 42.8 |
| `std::string::find`, pattern of 40 letters | 16.0 / 12.7 |

What to take from it:

- Building the hasher costs about one modular multiplication per character, and each query is two multiplications and some adds, independent of the substring length. A query on a 1000-letter substring is as cheap as one on a 10-letter substring.
- Rabin-Karp's time does not depend on the pattern length (the 40 and 1000 rows are about the same), as the theory says. It is **slower than the library's `find`** on ordinary text: the rolling update does two modular multiplications per character, and I did not profile `find`, which may skip ahead through the text. Use Rabin-Karp for what hashing makes possible, not for speed.
- The cost of the first hashing pass is what you pay once to make every later comparison `O(1)`; it pays off when you make many comparisons or search for many patterns.

## 7. Complexities

| Operation | Time | Space |
|---|---|---|
| Build prefix hashes and powers | `O(n)` | `O(n)` |
| Hash of any substring | `O(1)` | none |
| Compare two substrings | `O(1)` (probabilistic) | none |
| LCP of two suffixes | `O(log n)` | none |
| Rabin-Karp search, expected | `O(n + m)` | `O(1)` extra |
| Rabin-Karp, worst case with verification | `O(n m)` (when there are that many true matches) | `O(1)` |
| Longest duplicate / common substring | `O(n log n)` expected with a hash set; `O(n log^2 n)` with sorting | `O(n)` |
| Palindrome query | `O(1)` after `O(n)` | `O(n)` |

## 8. Which tool for which problem

- "Compare two substrings many times" or "do these two stretches match": prefix hashes.
- "Find one pattern with a guaranteed bound": [KMP or Z](01_kmp_and_the_z_function.md). "Find one pattern fast in practice": the library `find`.
- "Find many patterns of the *same length*": Rabin-Karp with a set of hashes (or, for a small alphabet and short length, an exact packed key: ten DNA letters fit in 20 bits, which is exact, with no collisions at all).
- "Longest repeated / common substring": binary search on the length with hashing, or a suffix array (12.06) if you need it deterministic.
- "Palindrome queries": hashes of the string and its reverse, or Manacher (12.03) for all centres at once.
- "Two strings equal as sets of rotations or after some transformation": hash a canonical form.
- If a wrong answer is unacceptable and you cannot verify: use a deterministic method.

## 9. Common mistakes

1. **Hashing modulo `2^64`** (unsigned overflow). Broken for every base by Thue-Morse strings.
2. **A fixed base, or a base the adversary can guess.** Choose it at random at run time.
3. **A small modulus.** `10^9 + 7` alone collides among 200,000 strings; use `2^61 - 1`, or two moduli.
4. **Forgetting the modulus in a subtraction**: `h[r] - h[l] * B^(r-l)` can go negative; use `sub61`, not `-`.
5. **A character value of 0**, so that leading zeros disappear. Use `c + 1`.
6. **Using the hash to order strings.** A polynomial hash tests *equality*, not less-than.
7. **Comparing hashes built with different bases or moduli.**
8. **Trusting a match without verification** where a false positive is costly. Verify, or accept the (tiny) risk knowingly.
9. **Overflow in the product `a * b`** for a modulus above `2^32` without a 128-bit type.
10. **An off-by-one in the powers table** (`B^(r-l)` against `B^(r-l-1)`), which the substring-versus-string tests catch at once.
11. **Believing the collision probability is `1/M` regardless of the strings.** It is at most `(L - 1) / M`, and the bound can be reached.
12. **Recomputing the pattern hash** for every query when the pattern is fixed.

## 10. Questions to answer out loud

1. Define the polynomial hash of a string. How do you get the hash of a substring in `O(1)`?
2. Write the rolling update for Rabin-Karp and explain each term.
3. What is the difference between a Las Vegas and a Monte Carlo version of Rabin-Karp?
4. Why is the collision probability at most `(L - 1) / M` for a random base? What assumption does it need?
5. With 200,000 strings and a 30-bit modulus, how many collisions do you expect? Why?
6. Why is hashing modulo `2^64` unsafe? What is the Thue-Morse attack?
7. Why must the base be random?
8. How do you find the longest duplicate substring in `O(n log n)`? Why can you binary search on the length?
9. How do you test whether `s[l..r)` is a palindrome in `O(1)`?
10. When would you choose KMP over Rabin-Karp, and the reverse?
11. How would you find any of 1000 patterns of the same length in one pass?

## 11. Problems

### 11.1 On paper

Answer first, then run the code to check.

**A.** With base 3, modulus 11, and the letter values `a = 1, b = 2, c = 3`, what is the polynomial hash of `bca`?

**B.** The window `abc` has the hash 123 with digit values 1, 2, 3, base 10, no modulus. Slide the window one step to `bcd` (`d = 4`) with the rolling update.

**C.** What is the collision probability bound for two different strings of length one million with a random base modulo `2^61 - 1`?

**D.** How many colliding pairs would you expect among `10^5` strings, and among `10^6` strings, with one hash modulo `10^9 + 7`?

**E.** What is the longest duplicate substring of `abcabcab`?

**F.** How many distinct substrings of length 2 does `abcabc` have?

**G.** Which of `racecar` and `abca` is a palindrome?

**H.** With verification, Rabin-Karp finds `aaa` in `aaaaaaaa`: how many matches, and how many character comparisons do the verifications use?

**I.** What is the longest common prefix of the suffixes of `abcabcabd` starting at 1 and at 4?

### 11.2 On LeetCode

Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | Where | How to approach |
|---|---|---|---|
| Find the Index of the First Occurrence in a String | 28 | LeetCode | Rabin-Karp with verification (and compare with KMP) |
| Repeated DNA Sequences | 187 | LeetCode | Windows of length 10: pack 2 bits per letter into an integer (exact), or roll a hash into a set |
| Longest Duplicate Substring | 1044 | LeetCode | Binary search on the length; hash the windows; use a 61-bit modulus or double hashing |
| Maximum Length of Repeated Subarray | 718 | LeetCode | Binary search on the length with a rolling hash on the arrays (or the `O(nm)` DP) |
| Longest Common Subpath | 1923 | LeetCode (hard) | Binary search on the length; one set of hashes per path, keep the intersection |
| Distinct Echo Substrings | 1316 | LeetCode | For each half length, hash both halves; compare with `get` |
| Longest Chunked Palindrome Decomposition | 1147 | LeetCode | Greedy: take the shortest prefix equal to the matching suffix, compared by hash |
| Find Substring With Given Hash Value | 2156 | LeetCode (hard) | A rolling hash computed from the right; compare each window's hash to the target |
| Longest Happy Prefix | 1392 | LeetCode | Compare prefix and suffix hashes for each length (or use the prefix function) |

### 11.3 In your editor

**A.** Implement double hashing with two moduli near `10^9` and compare its speed and collision count with the 61-bit version on 10 million strings.

**B.** Write the Thue-Morse attack yourself: generate the pair for a given length and show the collision for a few random odd bases under `unsigned long long` overflow, and no collision under `mod 2^61 - 1`.

**C.** Implement 2D Rabin-Karp: find an `a x b` pattern in a large grid by hashing rows, then columns of row-hashes.

**D.** Content-defined chunking (as used by `rsync`-style tools): roll a hash over a byte stream and cut a chunk whenever the hash modulo 8192 is 0; measure the distribution of chunk sizes and what happens to the chunk boundaries when you insert a byte near the start.

**E.** Find the longest palindromic substring with binary search on the radius around each centre, using `PalindromeQuery`, and compare with Manacher's algorithm when you reach 12.03.

### Answers

The program that checks the paper problems printed:

```
A 6
B 234
C 4.34e-13
D 5.0 500.0
E abcab
F 3
G 1 0
H 6 18
I 4
```

Reading it line by line:

**A.** `((2 * 3 + 3) * 3 + 1) mod 11 = 28 mod 11 = 6`.

**B.** `(123 - 1 * 100) * 10 + 4 = 234`, the number for `bcd`.

**C.** At most `(10^6 - 1) / (2^61 - 1)`, about `4.34 * 10^-13`.

**D.** `n^2 / (2M)`: `10^10 / (2 * 10^9) = 5` pairs for `10^5` strings, and `10^12 / (2 * 10^9) = 500` for `10^6`.

**E.** `abcab` (length 5): it occurs at positions 0 and 3, and the two copies overlap.

**F.** `3`: `ab`, `bc`, `ca`.

**G.** `racecar` is (printed `1`), `abca` is not (printed `0`).

**H.** 6 matches; each is confirmed by comparing 3 characters, so 18 character comparisons for verification. With a text of all `a` and a long pattern this is the `O(n m)` case, but those matches are real.

**I.** `4`: the suffixes `bcabcabd` and `bcabd` share `bcab`.

## Cheat sheet

- **Hash:** `H(s[l..r)) = s[l] B^(r-l-1) + ... + s[r-1] (mod M)`; prefix hashes `h[i+1] = h[i] B + s[i]`; `H(l, r) = h[r] - h[l] B^(r-l)`.
- **Modulus and base:** `M = 2^61 - 1` (reduce by adding high and low 61 bits), random base chosen at run time. Never `mod 2^64`.
- **Collision:** at most `(L - 1) / M` for a random base; many strings: about `n^2 / (2M)` colliding pairs.
- **Rabin-Karp:** `new = (old - t[i] B^(m-1)) B + t[i+m]`; verify for Las Vegas, trust for Monte Carlo; expected `O(n + m)`.
- **Longest duplicate / common substring:** binary search on the length (monotone), hash the windows, confirm neighbours. `O(n log n)`.
- **Palindrome query:** hash of `s[l..r)` equals hash of the matching stretch of `reverse(s)`.
- **In practice:** the library `find` is faster for one pattern; hashing earns its place when you compare many substrings.

---

## Sources

- All code is tested here (10,146,872 checks, GCC 13.3, sanitizers on); counts, traces and timings are measured on one Xeon container, two runs each. Times are this machine's.
- The Rabin-Karp algorithm (Karp and Rabin, 1987), polynomial hashing and the root-counting bound on the collision probability are standard; see CLRS (the chapter on string matching) and the usual universal-hashing treatments. The Thue-Morse attack on the `2^64` hash and the advice to use a random base are well known among competitive programmers (from memory; my tests reproduce the collisions measured above, and I cite no specific source). The Mersenne-prime reduction is a standard trick. The explanations and code are my own.
- LeetCode numbers and titles are from memory; check a number before you rely on it.
