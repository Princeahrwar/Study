# DSA 12.03: Palindromes and Manacher

*Part of the [String algorithms plan](00_plan.md) · Time: about 6 hours (3 reading, 3 problems) · You need: [String basics](../01_arrays_and_strings/07_string_basics.md) (section 3 grows palindromes outwards from each centre; this tutorial starts where it stops), [KMP and the Z-function](01_kmp_and_the_z_function.md) (the same "reuse what you already matched" idea), and [Rolling hash and Rabin-Karp](02_rolling_hash_and_rabin_karp.md) (palindrome queries with hashes, the competitor of section 4)*

A **palindrome** reads the same forwards and backwards. Checking one string is a two-pointer loop. The interesting questions are about *all* the palindromes inside a string: the longest one, how many there are, whether a given stretch is one, how to split the string into the fewest palindromes, or how many different ones occur. **Manacher's algorithm** answers all of them from one array computed in linear time, by reusing what is already known about the mirror image of the current position. A second structure, the **palindromic tree**, lists every *distinct* palindrome in linear time.

By the end you can:

- count the centres of a string and explain why the expand-around-centre method is quadratic on `aaaa...a`;
- write Manacher's algorithm (both the two-array form and the padded form) and explain why it is linear;
- answer "is `s[l..r)` a palindrome?" in `O(1)` after the linear pass;
- get the longest palindromic substring, the number of palindromic substrings, the fewest cuts into palindromes (LeetCode 132) and the shortest palindrome by prepending (LeetCode 214) from the radii;
- build a palindromic tree and read off the number of distinct palindromes and the occurrences of each;
- pick between expansion, a DP table, hashing, Manacher and the palindromic tree.

**Where the numbers come from.** The code below was compiled with `-Wall -Wextra` and tested: 5,037,037 checks passed under AddressSanitizer and UBSan (GCC 13.3). The tests ran, on 20,000 random strings over alphabets of 1 to 3 letters plus runs of one letter, Fibonacci strings and hand-picked strings: both radius arrays against their definitions; `is_palindrome` against a direct loop for **every substring**; the count and the longest palindrome against brute force; the padded form against the two-array form; `min_palindrome_cuts` against exhaustive search over all `2^(n-1)` ways to cut (for `n <= 14`); the longest palindromic prefix and shortest palindrome against direct definitions; and the palindromic tree against a `std::map` of every palindromic substring and its number of occurrences, rebuilding each node's string from the tree. I also broke the code on purpose in five ways (dropping the `r - i + 1` bound on the mirror, using the wrong mirror index for even palindromes, a wrong suffix link, a wrong even-length test, and propagating occurrence counts in the wrong order); the tests caught all five.

**Data and timing.** Inputs are synthetic and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are the **best of 3 repetitions, shown for run 1 / run 2**; they are this machine's, not yours. Counts and traces are the same in both runs.

---

## 1. Palindromes and centres

A string of length `n` has `n` single letters and `n - 1` gaps between neighbours, so **`2n - 1` centres**. An **odd** palindrome is centred on a letter (`aba`, centred on `b`); an **even** palindrome is centred on a gap (`abba`, centred between the two `b`'s). Every palindromic substring has exactly one centre, and if the palindrome of radius `k` around a centre exists, so do the ones of radius `k - 1`, `k - 2`, and so on down to the centre itself. That gives a compact way to describe all of them: for each centre, **how far does the palindrome extend?** Counting all palindromic substrings is then just adding those 2n - 1 extents up.

A string can contain up to `n(n + 1) / 2` palindromic substrings (all of `aaaa...a`), so you can never list them one by one in linear time; the radii are the compact answer.

## 2. Expand around the centre

The method from [String basics](../01_arrays_and_strings/07_string_basics.md): for each centre, compare `s[l]` with `s[r]` and move outwards while they match.

```cpp
// Baseline, O(n^2): grow outwards from each of the 2n - 1 centres (see String basics).  Returns (start, length) of the longest palindrome.
inline std::pair<int, int> longest_palindrome_expand(const string& s) {
    int n = (int)s.size(), bs = 0, bl = 0;
    for (int c = 0; c < 2 * n - 1; ++c) {
        int l = c / 2, r = l + c % 2;
        while (l >= 0 && r < n && s[l] == s[r]) { --l; ++r; }
        if (r - l - 1 > bl) { bl = r - l - 1; bs = l + 1; }
    }
    return {bs, bl};
}
```

It is correct and simple, and for ordinary text it is fast, since most centres stop after a character or two. Its worst case is a string like `aaaa...a`, where every centre runs to the end of the string: `O(n^2)`. Counting character comparisons on 2000 letters `a` (the Manacher count comes from an instrumented copy of the loop in the next section):

```
T1 a string of 2000 letters 'a': growing from every centre made 2001000 character comparisons; Manacher made 3997 (the radii agree with the library code: yes)
```

Two million comparisons for the quadratic method, about four thousand (twice the length) for Manacher. Manacher's insight is that these comparisons repeat work: inside a large palindrome, what happens around a position is a mirror image of what happened on the other side.

## 3. Manacher's algorithm

Define two arrays. `d1[i]` is the number of odd palindromes centred on letter `i` (equivalently the radius, counting the letter itself): they are `s[i-k+1 .. i+k-1]` for `k = 1 .. d1[i]`. `d2[i]` is the number of even palindromes centred between letters `i-1` and `i`: `s[i-k .. i+k-1]` for `k = 1 .. d2[i]`. For `abaaba`:

```
T2 s = abaaba: d1 = 1 2 1 1 2 1; d2 = 0 0 0 3 0 0   (d1[1] = 2: b and aba; d2[3] = 3: aa, baab, abaaba)
```

(`d1[1] = 2` is the palindromes `b` and `aba`; `d2[3] = 3` is `aa`, `baab` and `abaaba`.)

**The idea.** Keep `[l, r]`, the palindrome found so far whose right end reaches furthest. Take a new centre `i` inside it. Its **mirror** across the middle of `[l, r]` is `l + r - i`. Because `[l, r]` is a palindrome, the stretch around `i` is a reflection of the stretch around the mirror, so the palindrome at `i` is at least as long as the one at the mirror, *except* that it cannot be claimed to reach beyond `r` (we have not looked there). So start `i` at `min(d1[mirror], r - i + 1)` and compare only the characters beyond the window. If the palindrome at `i` extends past `r`, it becomes the new window.

```cpp
// Manacher's algorithm: all palindromes of s in O(n).
//   d1[i] = the number of palindromes of ODD length centred at letter i  (= the radius; they are s[i-k+1 .. i+k-1] for k = 1..d1[i])
//   d2[i] = the number of palindromes of EVEN length centred between letters i-1 and i  (s[i-k .. i+k-1] for k = 1..d2[i])
// Keep [l, r], the palindrome found so far that reaches furthest right.  A position i inside it has a mirror image l + r - i; the
// palindrome around i starts out at least as large as the one around the mirror (but no further than r), so only the part beyond r
// is compared character by character.  Every comparison that succeeds moves r right, so the total work is O(n).
struct Manacher {
    vector<int> d1, d2;
    explicit Manacher(const string& s) : d1(s.size()), d2(s.size()) {
        int n = (int)s.size();
        for (int i = 0, l = 0, r = -1; i < n; ++i) {                     // odd lengths
            int k = (i > r) ? 1 : std::min(d1[l + r - i], r - i + 1);
            while (i - k >= 0 && i + k < n && s[i - k] == s[i + k]) ++k;
            d1[i] = k--;
            if (i + k > r) { l = i - k; r = i + k; }
        }
        for (int i = 0, l = 0, r = -1; i < n; ++i) {                     // even lengths
            int k = (i > r) ? 0 : std::min(d2[l + r - i + 1], r - i + 1);
            while (i - k - 1 >= 0 && i + k < n && s[i - k - 1] == s[i + k]) ++k;
            d2[i] = k--;
            if (i + k > r) { l = i - k - 1; r = i + k; }
        }
    }
    // Is s[l..r) a palindrome?  O(1).  Odd length 2k - 1 needs d1 >= k at the middle letter; even length 2k needs d2 >= k at the right middle.
    bool is_palindrome(int l, int r) const {
        int len = r - l; if (len <= 1) return true;
        int mid = l + len / 2;
        return (len & 1) ? d1[mid] >= (len + 1) / 2 : d2[mid] >= len / 2;
    }
    long long count() const {                                            // number of palindromic substrings (equal ones at different places counted separately)
        long long c = 0; for (size_t i = 0; i < d1.size(); ++i) c += d1[i] + d2[i]; return c;
    }
    std::pair<int, int> longest() const {                                // (start, length) of a longest palindromic substring
        int bs = 0, bl = 0;
        for (int i = 0; i < (int)d1.size(); ++i) {
            if (2 * d1[i] - 1 > bl) { bl = 2 * d1[i] - 1; bs = i - d1[i] + 1; }
            if (2 * d2[i] > bl)     { bl = 2 * d2[i];     bs = i - d2[i]; }
        }
        return {bs, bl};
    }
};
// The same algorithm in the form most often taught: write a separator between the letters (#a#b#a#) so that every palindrome has odd
// length and a letter as its centre.  p[i] = the length, in s, of the longest palindrome centred at position i of the padded string.
inline vector<int> manacher_padded(const string& s) {
    string t = "#"; for (char c : s) { t += c; t += '#'; }
    int n = (int)t.size(), c = 0, r = 0; vector<int> p(n, 0);            // [c - r, c + r] is the furthest-reaching palindrome so far
    for (int i = 0; i < n; ++i) {
        if (i < r) p[i] = std::min(r - i, p[2 * c - i]);
        while (i - p[i] - 1 >= 0 && i + p[i] + 1 < n && t[i - p[i] - 1] == t[i + p[i] + 1]) ++p[i];
        if (i + p[i] > r) { c = i; r = i + p[i]; }
    }
    return p;
}
```

The odd radii of `abacaba`, one position at a time, showing where each starts:

```
T3 s = abacaba, odd radii step by step: [i=0: outside the window, start 1, final 1] [i=1: outside the window, start 1, final 2] [i=2: mirror 0, start 1, final 1] [i=3: outside the window, start 1, final 4] [i=4: mirror 2, start 1, final 1] [i=5: mirror 1, start 2, final 2] [i=6: mirror 0, start 1, final 1]
```

At `i = 3` (the letter `c`) nothing is known yet and the palindrome grows to radius 4, the whole string. After that, every later centre is *inside* that window and starts from its mirror's value; position 4 starts at the value for position 2 (1), 5 at that of position 1 (2), 6 at position 0 (1), and none of them needs to grow.

**Why it is linear.** The right end `r` only moves to the right. Every character comparison that succeeds pushes `r` right by one position; the comparison that fails costs one per centre. So the total number of comparisons is at most about `2n` per pass, however the string looks. (This is the same amortised argument as for KMP's fallbacks in [12.01](01_kmp_and_the_z_function.md).)

**The padded form.** Many courses teach a version with one array: write a separator between all letters, so `abaaba` becomes `#a#b#a#a#b#a#`. Every palindrome of the padded string has odd length and a *character* as its centre (a letter, or the `#` standing for a gap), so the odd/even distinction disappears. `p[i]` is then the length, in the original string, of the longest palindrome centred at `i`.

```
T4 padded form #a#b#a#a#b#a#, palindrome length in s at each position: 0 1 0 3 0 1 6 1 0 3 0 1 0
```

The `6` in the middle is the whole of `abaaba`. The relation to the two-array form is exact (`p[2i+1] = 2 d1[i] - 1` and `p[2i] = 2 d2[i]`, which the tests check); use whichever you can write more reliably from memory. The padded form is shorter to code, the two-array form avoids building a second string.

## 4. What you can read off the radii

```cpp
// LeetCode 132: the fewest cuts that split s into palindromes.  cuts[i] = fewest cuts for the prefix s[0..i); try every last piece
// s[j..i) that is a palindrome (an O(1) query).  O(n^2) time.
inline int min_palindrome_cuts(const string& s) {
    int n = (int)s.size(); if (n == 0) return 0;
    Manacher M(s); vector<int> cuts(n + 1, 0);
    for (int i = 1; i <= n; ++i) {
        cuts[i] = i - 1;                                                 // at worst, cut after every letter
        for (int j = 0; j < i; ++j) if (M.is_palindrome(j, i)) cuts[i] = std::min(cuts[i], j == 0 ? 0 : cuts[j] + 1);
    }
    return cuts[n];
}
// The longest palindromic PREFIX of s, from the radii.  Then LeetCode 214 (shortest palindrome by adding characters in front) is the
// reverse of the remaining suffix, put in front.
inline int longest_palindromic_prefix(const string& s) {
    Manacher M(s);
    for (int len = (int)s.size(); len >= 1; --len) if (M.is_palindrome(0, len)) return len;
    return 0;
}
inline string shortest_palindrome_manacher(const string& s) {
    int k = longest_palindromic_prefix(s);
    return string(s.rbegin(), s.rend() - k) + s;
}
```

(`Manacher` also has `is_palindrome`, `count` and `longest`, shown in the code above.)

- **Longest palindromic substring** (LeetCode 5): the largest `2 d1[i] - 1` or `2 d2[i]`. For `babad` and `cbbd`:

```
T5 longest palindromic substring: babad -> bab (start 0, length 3); cbbd -> bb (start 1, length 2)
```

- **Number of palindromic substrings** (LeetCode 647): the sum of all the `d1` and `d2` values, since each value counts the palindromes at that centre.

```
T6 palindromic substrings counted (equal ones at different places count separately): aaa -> 6; abc -> 3; abacaba -> 12
```

(`abacaba`: 7 single letters, `aba` twice, `aca`, `bacab` and `abacaba`, which totals 12.)

- **Is `s[l..r)` a palindrome?** In `O(1)`: its length is `len`, its middle is `l + len / 2`, and it is a palindrome exactly when the radius there is large enough (`d1[mid] >= (len + 1) / 2` for odd `len`, `d2[mid] >= len / 2` for even). Compare the hashing method of [12.02](02_rolling_hash_and_rabin_karp.md), which is probabilistic; this is exact.

```
T7 abacabad: is_palindrome(0,7) = 1 (abacaba); (1,4) = 0 (bac); (0,8) = 0; (2,5) = 1 (aca); (3,3) = 1 (empty)
```

- **Fewest cuts into palindromes** (LeetCode 132): `cuts[i]` is the fewest cuts for the prefix of length `i`; try each last piece `s[j..i)` that is a palindrome (an `O(1)` query now) and take `cuts[j] + 1`. Total `O(n^2)`; with the quadratic palindrome *table* it would also be `O(n^2)` time but `O(n^2)` memory. There are faster methods (using the structure of palindromic suffixes), which I do not cover.

```
T8 fewest cuts into palindromes: aab -> 1 (aa|b); abcd -> 3; racecar -> 0; ababbbabbababa -> 3
```

- **Shortest palindrome by adding characters in front** (LeetCode 214): find the longest palindromic *prefix* `P` of `s` (here by testing prefixes from longest to shortest with `is_palindrome`), then put the reverse of the rest in front. This is the third way you now have to solve it: the prefix function of [12.01](01_kmp_and_the_z_function.md) in `O(n)`, hashing, or the radii.

```
T9 shortest palindrome by prepending (from the radii): aacecaaa -> aaacecaaa; abcd -> dcbabcd; abb -> bbabb
```

(My loop over prefix lengths is `O(n)` queries of `O(1)` each. The tests compare it with a direct definition.)

## 5. The palindromic tree (stretch)

Manacher counts palindromes by *position*. Sometimes you need the **distinct** ones: "how many different palindromic substrings are there?" A string of length `n` has **at most `n` distinct palindromic substrings** (each new letter adds at most one new palindrome, the longest palindromic suffix; a known result I take from the literature, and the tests never saw a string that broke it). The **palindromic tree**, or *eertree*, stores them all, building online one letter at a time.

- A **node** is a distinct palindrome. The two roots are special: node 0 is an imaginary palindrome of length `-1` (a child of it is a single letter `c`), node 1 is the empty palindrome of length 0 (a child of it is `cc`).
- An **edge** labelled `c` from node `P` leads to the node for `c P c`.
- A **suffix link** from a node goes to its longest *proper* palindromic suffix.

To append a letter, find the longest palindromic suffix of the text that the new letter can be wrapped around: start from the longest palindromic suffix so far and follow suffix links until the character just before it equals the new letter. Wrap, creating a node if that palindrome is new, and set its suffix link by the same walk from the parent's link. Each step down the links is paid for by an earlier step up, so a letter costs amortised `O(1)`.

```cpp
// The palindromic tree ("eertree"): a node for every DISTINCT palindromic substring of s, built online, one letter at a time.
// A string of length n has at most n distinct palindromic substrings, so the tree has at most n + 2 nodes.
//   node 0: the "imaginary" palindrome of length -1 (a child of it is a single letter);  node 1: the empty palindrome (a child is cc)
//   next[c]: the palindrome c + P + c  for the node P;   link: the longest PROPER palindromic suffix.
// Adding a letter: walk down the suffix links from the longest palindromic suffix of the text so far, until a node P is found such that
// the letter before P equals the new letter; then c + P + c is the new longest palindromic suffix.  Amortised O(1) per letter.
struct Eertree {
    struct Node { int len, link; std::array<int, 26> next; };
    vector<Node> t; vector<long long> cnt; string s; int last;
    Eertree() : last(1) { t.push_back({-1, 0, {}}); t.push_back({0, 0, {}}); cnt.assign(2, 0); }
    int find(int v, int i) const {                                       // first suffix-palindrome v (following links) that s[i] can extend
        while (!(i - 1 - t[v].len >= 0 && s[i - 1 - t[v].len] == s[i])) v = t[v].link;
        return v;
    }
    void add(char ch) {
        s.push_back(ch); int i = (int)s.size() - 1, c = ch - 'a';
        int cur = find(last, i);
        if (t[cur].next[c] == 0) {
            int nw = (int)t.size();
            t.push_back({t[cur].len + 2, 0, {}}); cnt.push_back(0);
            t[nw].link = (t[nw].len == 1) ? 1 : t[find(t[cur].link, i)].next[c];
            t[cur].next[c] = nw;
        }
        last = t[cur].next[c]; ++cnt[last];
    }
    int distinct() const { return (int)t.size() - 2; }                   // number of distinct palindromic substrings
    // occurrences of every palindrome: a palindrome that ends at some position also contains its suffix-link chain
    vector<long long> occurrences() const {
        vector<long long> c = cnt;
        for (int v = (int)t.size() - 1; v >= 2; --v) c[t[v].link] += c[v];
        return c;
    }
};
```

Occurrence counts come from a second step: each position ends at its longest palindromic suffix, and that palindrome also contains all the palindromes along its suffix-link chain, so adding each node's count into its link's count, from the newest node backwards, gives the number of occurrences of every palindrome. On `eertree`:

```
T10 palindromic tree of eertree: 7 distinct palindromes, nodes (length, longest proper palindromic suffix link): [node 2: len 1, link 1, occurs 4] [node 3: len 2, link 2, occurs 2] [node 4: len 1, link 1, occurs 2] [node 5: len 1, link 1, occurs 1] [node 6: len 3, link 4, occurs 1] [node 7: len 5, link 2, occurs 1] [node 8: len 7, link 3, occurs 1]
```

The seven palindromes are `e`, `r`, `t`, `ee`, `rtr`, `ertre` and the whole string `eertree`. Node 2, the letter `e`, occurs 4 times. Two more checks:

```
T11 distinct palindromic substrings: abaaba -> 6 (a, b, aa, aba, baab, abaaba); aaaaaaaaaa -> 10 (never more than the length)
```

```
T12 palindromic tree of a random string of 1000000 letters over 3 symbols has 4397 distinct palindromes
```

**Memory.** Each node here holds a 26-entry table of ints, 112 bytes per node, so a string with a million distinct palindromes (a million letters `a`) uses over 100 MB; a random string over a few letters has few distinct palindromes and uses almost nothing. A map or a sorted edge list per node is the usual fix for large alphabets.

## 6. Which method

| Method | Time | Space | Exact? | Gives |
|---|---|---|---|---|
| Expand around centres | `O(n^2)` worst, fast on ordinary text | `O(1)` | yes | longest, count |
| DP table `isPal[i][j]` | `O(n^2)` | `O(n^2)` | yes | any substring query |
| Hash of `s` and `reverse(s)` ([12.02](02_rolling_hash_and_rabin_karp.md)) | `O(n)` set-up, `O(1)` query | `O(n)` | probabilistic | any substring query |
| Manacher | `O(n)` | `O(n)` | yes | longest, count, any substring query |
| Palindromic tree | `O(n)` (alphabet constant) | `O(n)` nodes | yes | distinct count, occurrences |

## 7. Measured costs

Machine and method as above.

| Case | Time, ms (run 1 / run 2) |
|---|---|
| Expand around centres, 30,000 letters `a` (worst case) | 213.2 / 172.6 |
| Manacher, the same input | 0.3 / 0.2 |
| Manacher (two arrays), 10,000,000 letters `a` | 124.0 / 117.2 |
| Manacher (padded form), 10,000,000 letters `a` | 125.4 / 122.5 |
| Manacher (two arrays), 10,000,000 random letters over `a`, `b` | 224.4 / 221.2 |
| Manacher (padded form), the same | 179.1 / 217.2 |
| Expand around centres, 100,000 random letters over 26 symbols | 0.3 / 0.4 |
| Manacher, the same | 0.4 / 0.5 |
| Palindromic tree, 1,000,000 random letters over 3 symbols | 14.7 / 18.9 |
| Palindromic tree, 1,000,000 letters `a` | 90.4 / 111.5 |
| `min_palindrome_cuts`, 5000 random letters over `a`, `b` | 8.5 / 10.8 |

- On the worst case the gap is enormous (expansion is about 700 to 900 times slower at 30,000 letters, and the gap grows with `n`); on ordinary random text over 26 letters the simple expansion is as fast as Manacher, because almost every centre stops at once. Pick Manacher when you need the guarantee or the radii.
- Manacher processes ten million letters in a fraction of a second; the two forms are about the same speed. The random two-letter input is slower than the all-`a` input, plausibly because the comparisons are less predictable for the processor, but I did not measure that.
- The palindromic tree on a random 3-letter string had only 4,397 distinct palindromes among a million letters (the line above), so it is fast and small; on `aaaa...a` it has a million nodes, which is the slow and memory-heavy case.

## 8. Complexities

| Operation | Time | Space |
|---|---|---|
| Radii `d1`, `d2` (Manacher) | `O(n)` | `O(n)` |
| Is `s[l..r)` a palindrome | `O(1)` after the radii | none |
| Longest palindromic substring, count of palindromic substrings | `O(n)` | `O(n)` |
| Fewest palindromic cuts | `O(n^2)` | `O(n)` |
| Palindromic tree, build | `O(n)` amortised (constant alphabet) | `O(n)` nodes |

## 9. Which tool for which problem

- "Longest palindromic substring": expansion if the input is ordinary and small; Manacher if the input can be adversarial or large.
- "How many palindromic substrings": sum of the radii (or the palindromic tree's occurrence counts).
- "Is this range a palindrome" many times: Manacher radii (exact) or hashes (probabilistic, but also usable when other queries need hashing).
- "Split into palindromes": a DP that uses a palindrome query: cuts (min), enumeration (all splits, with backtracking).
- "Palindromic subsequence" (not substring): that is a DP, longest common subsequence of `s` and its reverse ([DP 03](../10_dynamic_programming/03_grid_and_two_sequence_dp.md)); none of this tutorial applies.
- "Shortest palindrome by adding at the front": prefix function, hash, or radii.
- "How many *different* palindromes": the palindromic tree.

## 10. Common mistakes

1. **Confusing substring with subsequence.** Manacher is for contiguous palindromes.
2. **Forgetting the even centres**, or the odd ones: both arrays are needed (or the padded form).
3. **A wrong mirror index.** For odd palindromes the mirror of `i` is `l + r - i`; for even ones, with `d2` centred between letters, it is `l + r - i + 1`.
4. **Not capping the mirror value at `r - i + 1`** (or `r - i`): the stretch beyond `r` has not been checked.
5. **Off-by-one in the final extraction**: the start of an odd palindrome is `i - d1[i] + 1`, of an even one `i - d2[i]`.
6. **Counting in the padded form** without halving: `p[i]` is the length in the original string, not the radius in the padded one (the two agree in the form used above; in other versions they differ by one).
7. **Assuming the expansion method is always slow**, or always fast. It is quadratic only on highly repetitive input.
8. **Using a hash comparison where an exact answer is needed.**
9. **A palindromic tree with a 26-entry table for a huge alphabet or a very large input**, which can run out of memory.
10. **Recomputing the radii for each query** instead of once.
11. **A DP table of `n^2` booleans for a long string** when Manacher answers the same queries in `O(n)` memory.
12. **Treating the empty string and single letters as non-palindromes.** Both are palindromes; check what the problem counts.

## 11. Questions to answer out loud

1. How many centres does a string of length `n` have? Why odd and even?
2. Why is expansion around centres quadratic on `aaaa...a`? Give the comparison count.
3. Explain Manacher's mirror idea. What is the window `[l, r]`?
4. Why is Manacher linear, even though it has a `while` inside a `for`?
5. How are `d1` and `d2` related to the padded array `p`?
6. How do you test that `s[l..r)` is a palindrome in `O(1)`?
7. How do you count all palindromic substrings from the radii?
8. How do you compute the fewest cuts into palindromes, and what is its complexity?
9. Give three ways to solve "shortest palindrome by prepending".
10. What is a palindromic tree? What do its edges and suffix links mean?
11. Why are there at most `n` distinct palindromic substrings?
12. When would you use hashing, Manacher or the tree for palindromes?

## 12. Problems

### 12.1 On paper

Answer first, then run the code to check.

**A.** Compute `d1` and `d2` for `aabaa`.

**B.** How many palindromic substrings does `abba` have?

**C.** What is the longest palindromic substring of `forgeeksskeegfor`?

**D.** In `abcba`, is the stretch `[1, 4)` a palindrome? Is `[0, 4)`?

**E.** What is the fewest number of cuts that splits `abbab` into palindromes?

**F.** What is the shortest palindrome you can make from `race` by adding characters in front?

**G.** How many distinct palindromic substrings does `aabaa` have?

**H.** Write the padded-form array `p` for the string `aa`.

**I.** How many palindromic substrings do `aaaaaaaaaa` (ten letters) and a hundred letters `a` have?

### 12.2 On LeetCode

Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | Where | How to approach |
|---|---|---|---|
| Longest Palindromic Substring | 5 | LeetCode | Expansion or Manacher |
| Palindromic Substrings | 647 | LeetCode | Sum of the radii |
| Palindrome Partitioning | 131 | LeetCode | Backtracking, with an `O(1)` palindrome query for each piece |
| Palindrome Partitioning II | 132 | LeetCode | The cuts DP with radii (or the expansion-based DP) |
| Palindrome Partitioning IV | 1745 | LeetCode | Choose two cut points; test the three pieces with `is_palindrome` |
| Shortest Palindrome | 214 | LeetCode | Longest palindromic prefix: prefix function, hash or radii |
| Maximum Number of Non-overlapping Palindrome Substrings | 2472 | LeetCode (hard) | Greedy: finish the earliest palindrome of length at least `k` (expansion from centres) |
| Palindrome Pairs | 336 | LeetCode (hard) | For each word, split it at every point; if one part is a palindrome (radii) look up the reverse of the other part in a hash map |
| Maximum Product of the Length of Two Palindromic Substrings | 1960 | LeetCode (hard) | Manacher radii; best odd palindrome ending at or before each index and starting at or after each index |

### 12.3 In your editor

**A.** Implement Manacher from memory (either form) and test it against expansion on random strings.

**B.** Write an `O(n^2)` DP table `isPal[i][j]` and compare its memory and time with the radii for `n = 20,000`.

**C.** With the palindromic tree, compute for every prefix the length of its longest palindromic suffix, and use it to find the fewest cuts in a different way.

**D.** Count the palindromic substrings of a string with a hash and binary search per centre (using `PalindromeQuery` from 12.02) and compare its running time with Manacher.

**E.** Using the tree, count the distinct palindromes of a Fibonacci string and a Thue-Morse string; compare with the bound `n`.

### Answers

The program that checks the paper problems printed:

```
A d1 = 1 1 3 1 1; d2 = 0 1 0 0 1
B 6
C geeksskeeg 10
D 1 0
E 1
F ecarace
G 5
H 0 1 2 1 0
I 55 5050
```

Reading it line by line:

**A.** `d1 = 1 1 3 1 1` (the middle `b` has radius 3: `b`, `aba`, `aabaa`) and `d2 = 0 1 0 0 1` (the gaps inside the two `aa` pairs).

**B.** `6`: four letters, `bb`, and `abba`.

**C.** `geeksskeeg`, length 10.

**D.** `bcb` is a palindrome (printed `1`); `abcb` is not (printed `0`).

**E.** `1`: `abba|b`.

**F.** `ecarace`: the longest palindromic prefix of `race` is `r`, so the reverse of `ace` goes in front.

**G.** `5`: `a`, `b`, `aa`, `aba`, `aabaa`.

**H.** `0 1 2 1 0`: the padded string is `#a#a#`, and the middle `#` is the centre of `aa` (length 2).

**I.** `55` and `5050`: a string of `n` equal letters has `n(n + 1) / 2` palindromic substrings.

## Cheat sheet

- **Centres:** `2n - 1` (n letters, n - 1 gaps). `d1[i]` odd palindromes at letter `i`; `d2[i]` even palindromes between `i-1` and `i`.
- **Manacher:** keep the furthest-reaching palindrome `[l, r]`; for `i` inside, start from `min(d[mirror], r - i + 1)`, then extend by comparing. `O(n)` because `r` only moves right.
- **Odd mirror** `l + r - i`; **even mirror** `l + r - i + 1`. Padded form: `p[2i+1] = 2 d1[i] - 1`, `p[2i] = 2 d2[i]`.
- **Longest:** max of `2 d1 - 1` and `2 d2`. **Count:** sum of all `d1 + d2`. **Is palindrome:** radius at the middle is large enough.
- **Fewest cuts:** `cuts[i] = min over palindromic s[j..i) of cuts[j] + 1`, `O(n^2)`.
- **Shortest palindrome by prepending:** longest palindromic prefix, then reverse the rest.
- **Palindromic tree:** nodes are distinct palindromes, edge `c` adds one letter on each side, suffix link = longest proper palindromic suffix; at most `n` distinct; amortised `O(1)` per letter.
- **Expansion is fine** on ordinary text and quadratic only on repetitive text.

---

## Sources

- All code is tested here (5,037,037 checks, GCC 13.3, sanitizers on); counts, traces and timings are measured on one Xeon container, two runs each. Times are this machine's.
- Manacher's algorithm (1975) is standard; the two-array form follows the usual competitive-programming write-up, from memory, and the padded form is the one found in many courses. The palindromic tree is due to Rubinchik and Shur (2014, from memory); the bound of at most `n` distinct palindromes in a string of length `n` is a known result (Droubay, Justin and Pirillo, 2001, from memory), which my tests never contradicted. The explanations and code are my own.
- LeetCode numbers and titles are from memory; check a number before you rely on it.
