# DSA 10.02: One-Dimensional DP

*Part of the [Dynamic programming plan](00_plan.md) · Time: about 8 hours (4 reading, 4 problems) · You need: [DP fundamentals](01_dp_fundamentals.md) (state, transition, the three forms) and [Binary search templates](../03_binary_search_and_sorting/01_binary_search_templates.md) (for the O(n log n) longest increasing subsequence). There is no companion demo file: every structure is listed in this doc

A **one-dimensional DP** keeps its answers in a single array: one entry per amount, per prefix, or per ending position. Most interview DP problems that are not on a grid or on two strings are of this kind, and they fall into three recurring shapes. Learn the shapes and the problems stop looking different:

1. **The amount is the state** (coin change, perfect squares): `f(x)` for every total `x`, built from `f(x - something)`.
2. **The prefix is the state** (word break, decode ways): `f(i)` for the first `i` characters, built from shorter prefixes.
3. **The ending position is the state** (maximum subarray, longest increasing subsequence): `f(i)` is the best answer for a structure that **ends at** `i`, and the final answer is the best over all `i`.

By the end you can:

- solve coin change three ways (fewest coins, number of combinations, number of ordered sequences) and explain exactly why swapping two loops changes the answer from one count to the other;
- solve perfect squares, word break and decode ways, including the traps around `'0'` and around substring cost;
- write Kadane's algorithm and its product variant, and explain why the product needs two values;
- write the longest increasing subsequence in O(n^2), in O(n log n) with the `tails` array, recover an actual subsequence, and count the longest ones;
- say for each problem what the state means in words and which earlier states the transition reads.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 59,817 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: coin combinations and ordered sequences against exhaustive enumeration, and feasibility against the same; the euro-coin count of 73,682 ways to make 200, and a modular count at amount 5,000 computed independently; perfect squares against the four-square theorem (Lagrange and Legendre) for 700 values up to 20,000; word break against plain recursion on random strings and dictionaries, with and without the length bound; decode ways against enumeration of every decoding on random digit strings with many zeros; maximum subarray (with its indices checked) and maximum product against all O(n^2) subarrays, and at one million elements against a prefix-minimum formula; the longest increasing subsequence (strict and non-strict), its reconstruction and the count of longest ones against every subset, and at n = 3,000 against the O(n^2) table; and pins for the cases random inputs rarely hit (all-negative arrays, ties between equal subarrays, a prefix of sum 0, strings with a leading or embedded `'0'`, an empty input, a word longer than the text, and results beyond 64 bits without the modulus). Then 45 hand-made mutants of the listings were run against the tests: **42 were caught and 3 are equivalent changes**; section 7 says which survived and what that means.

**Data and timing.** Instances are **synthetic** and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are **run 1 / run 2**, one measurement per row per run, and a claim is made only when it holds in both; they are this machine's, not yours. Counts and answers are identical in both runs.

---

## 1. Coin change and perfect squares: the amount is the state

You have coins and a target amount `x`. Three different questions share one state `f(x)`:

- **Fewest coins** that make `x`: `f(x) = 1 + min f(x - c)` over the coins `c <= x`, with `f(0) = 0` and "infinity" for amounts that cannot be made.
- **Number of combinations** (the multiset `{1, 2}` counts once, however you order it).
- **Number of ordered sequences** (`1, 2` and `2, 1` count separately).

```cpp
// Coin change, three questions about the same coins. State f(x) = the answer for amount x.
// (1) Fewest coins:  f(x) = 1 + min over coins c <= x of f(x - c).   -1 if x cannot be made.
int coin_fewest(const std::vector<int>& coins, int amount) {
    const int INF = INT_MAX / 2; std::vector<int> f(amount + 1, INF); f[0] = 0;
    for (int x = 1; x <= amount; ++x) for (int c : coins) if (c > 0 && c <= x && f[x - c] + 1 < f[x]) f[x] = f[x - c] + 1;
    return f[amount] >= INF ? -1 : f[amount];
}
// (2) Number of COMBINATIONS (1+2 and 2+1 are the same): the coin loop is OUTSIDE. Each coin is considered once, in a fixed order,
//     so every multiset is built in exactly one way.
ll coin_combinations(const std::vector<int>& coins, int amount) {
    std::vector<ll> w(amount + 1, 0); w[0] = 1;
    for (int c : coins) if (c > 0) for (int x = c; x <= amount; ++x) w[x] = (w[x] + w[x - c]) % MOD;
    return w[amount];
}
// (3) Number of ORDERED sequences (1+2 and 2+1 count twice): the amount loop is OUTSIDE. For each x every coin may be the LAST one.
ll coin_sequences(const std::vector<int>& coins, int amount) {
    std::vector<ll> w(amount + 1, 0); w[0] = 1;
    for (int x = 1; x <= amount; ++x) for (int c : coins) if (c > 0 && c <= x) w[x] = (w[x] + w[x - c]) % MOD;
    return w[amount];
}
// Perfect squares: fewest squares summing to n.  f(x) = 1 + min over k with k*k <= x of f(x - k*k).  (Never more than 4.)
int perfect_squares(int n) {
    std::vector<int> f(n + 1, 0);
    for (int x = 1; x <= n; ++x) { f[x] = INT_MAX; for (int k = 1; k * k <= x; ++k) f[x] = std::min(f[x], f[x - k * k] + 1); }
    return f[n];
}
```

The two counts differ only in **which loop is outside**, and that is worth understanding rather than memorising.

- *Coin loop outside, amount loop inside (combinations).* When coin `c` is processed, `w[x] += w[x - c]` extends the ways that used only the earlier coins (and some of coin `c`) by one more `c`. Each coin is considered once, in a fixed order, so every multiset is built in exactly one way: the order of the coins in the list is the canonical order of the multiset.
- *Amount loop outside, coin loop inside (sequences).* For each `x`, every coin may be the **last** one: `w[x] = sum of w[x - c]`. The last element of a sequence is a free choice at every position, so each ordering is counted.

Trace (checked by running the code):

```
combinations after coins up to 1: 1 1 1 1 1 1
combinations after coins up to 2: 1 1 2 2 3 3
combinations after coins up to 5: 1 1 2 2 3 4
ordered sequences for amount 0..5: 1 1 2 3 5 9
amount 5 with coins 1 2 5: fewest=1 combinations=4 sequences=9
```

For amount 5 with coins 1, 2, 5 the combinations are `5`, `2+2+1`, `2+1+1+1` and `1+1+1+1+1`: four, which the last table entry shows; the sequences are nine. At amount 20 the same coins give 29 combinations and 26,547 ordered sequences, so confusing the two is not a small error. The classic check: with the eight euro coins `1, 2, 5, 10, 20, 50, 100, 200`, there are 73,682 combinations for 200 (a well-known count, reproduced here). Sequences of 1s and 2s alone follow the Fibonacci numbers, and the tests use that as an independent check at amount 5,000.

The counts grow fast, so they are taken modulo `1,000,000,007`. The tests include a case that exceeds 64 bits without the modulus, because a missing `% MOD` is invisible on small amounts.

**Perfect squares.** The fewest perfect squares summing to `n` is coin change with the coins `1, 4, 9, 16, ...`. The table is O(n sqrt n). Lagrange's theorem says the answer is never more than 4, and Legendre's three-square theorem says it is exactly 4 when `n = 4^a (8b + 7)`. The tests use these theorems as an independent check. Over every `n` up to 100,000, the maximum was 4, and the values 1, 2, 3 and 4 occurred 316, 23,712, 59,308 and 16,664 times; the count for 4 equals the number of `n` of the form `4^a (8b + 7)` in that range (16,664), as the theorem predicts.

```
fewest squares for 1..13: 1 2 3 1 2 3 4 2 1 2 3 3 2
```

Coin change with one million as the amount and the coins `1, 5, 10, 25, 50, 100` took 11.0 / 6.9 ms for the fewest coins (the answer is 10,000, a hundred-unit coin used ten thousand times) and 18.9 / 15.8 ms for the number of combinations: linear in the amount times the number of coins.

## 2. Word break and decode ways: the prefix is the state

**Word break.** Can a string be split into dictionary words? `f(i)` = the first `i` characters can be split; `f(0) = true`; `f(i)` is true if some `j < i` has `f(j)` true and `s[j..i)` is a word. The inner loop is the cost. A substring longer than the longest word cannot be a word, so `j` only needs to go back that far: the bound turns O(n^2) substring work into O(n times the longest word). On a text of 5,000 letters with no possible split and a dictionary whose longest word has 50 letters, the bounded version took 0.20 / 0.22 ms and the unbounded one 6.8 / 7.4 ms, with the same answer (false).

**Decode ways.** `A = 1`, ..., `Z = 26`. A digit string can be read in several ways. `f(i)` = ways to read the first `i` digits; `f(0) = 1` (the empty reading, which is also why the empty string has one decoding in this code). The last letter is either one digit (allowed unless it is `'0'`: add `f(i-1)`) or two digits (allowed when they form 10 to 26, and the first is not `'0'`: add `f(i-2)`). A lone `'0'` cannot be read, and `"06"` is not a valid two-digit code even though 6 is a letter.

```cpp
// Word break: can s be split into dictionary words? State f(i) = the first i characters can be split.  f(0) = true.
// f(i) = OR over j < i of (f(j) and s[j..i) is a word).  Only lengths up to the longest word can be words, so j is limited to that:
// without the bound the loop does O(n^2) substring work.
bool word_break(const std::string& s, const std::vector<std::string>& words, bool bound = true) {
    std::unordered_set<std::string> dict(words.begin(), words.end()); size_t longest = 0; for (auto& w : words) longest = std::max(longest, w.size());
    int n = (int)s.size(); std::vector<char> f(n + 1, 0); f[0] = 1;
    for (int i = 1; i <= n; ++i) for (int len = 1; len <= i && (!bound || (size_t)len <= longest); ++len)
        if (f[i - len] && dict.count(s.substr(i - len, len))) { f[i] = 1; break; }
    return f[n];
}
// Decode ways: A=1 ... Z=26. How many ways to read a digit string? f(i) = ways for the first i digits.  f(0) = 1 (the empty reading).
// Take one digit if it is not '0': add f(i-1).  Take two digits if they form 10..26: add f(i-2).  A lone '0' cannot start a letter.
ll decode_ways(const std::string& s) {
    int n = (int)s.size(); std::vector<ll> f(n + 1, 0); f[0] = 1;
    for (int i = 1; i <= n; ++i) {
        if (s[i - 1] != '0') f[i] = f[i - 1];
        if (i >= 2) { int v = (s[i - 2] - '0') * 10 + (s[i - 1] - '0'); if (s[i - 2] != '0' && v <= 26) f[i] = (f[i] + f[i - 2]) % MOD; }
    }
    return f[n];
}
```

```
catsandog with cats dog sand and cat, prefix can be split (length 0..9): 1 0 0 1 1 0 0 1 0 0
leetcode with leet code: 1
decode 226, ways for each prefix (length 0..3): 1 1 2 3
decode 06=0 decode 11106=2 decode 2101=1
```

The zero rules are where nearly everyone fails first. On 100,000 random 6-digit strings, a decoder that adds `f(i-1)` always and `f(i-2)` when the two digits are 10 to 26, but forgets that a lone `'0'` cannot be read, was wrong on 47,062 (47.1%).

## 3. Kadane and the product: the end is the state

**Maximum subarray.** `f(i)` = the best sum of a non-empty subarray **ending at** `i`. Either it extends the best one ending at `i-1`, or it starts fresh: `f(i) = max(a[i], f(i-1) + a[i])`. Equivalently: when the running sum is negative, drop it. The answer is the maximum of `f(i)` over `i`. That is Kadane's algorithm, one pass and O(1) space.

**Maximum product.** The same state does not work, because a very negative product multiplied by a negative number becomes the largest. So the state carries **two** values for each end: the largest and the smallest product of a subarray ending here. A negative `a[i]` swaps their roles.

```cpp
// Maximum subarray (non-empty): f(i) = best sum of a subarray ENDING at i = max(a[i], f(i-1) + a[i]). Answer: max over i.
// f(i-1) is dropped as soon as it is negative: carrying a loss forward never helps.
struct Sub { ll sum; int l, r; };
Sub max_subarray(const std::vector<ll>& a) {
    Sub best = {a[0], 0, 0}; ll cur = a[0]; int start = 0;
    for (int i = 1; i < (int)a.size(); ++i) {
        if (cur < 0) { cur = a[i]; start = i; } else cur += a[i];
        if (cur > best.sum) best = {cur, start, i};
    }
    return best;
}
// Maximum PRODUCT subarray: a negative number turns the smallest product into the largest, so two values are kept for each
// end: the largest and the smallest product of a subarray ending here.
ll max_product_subarray(const std::vector<ll>& a) {
    ll hi = a[0], lo = a[0], best = a[0];
    for (int i = 1; i < (int)a.size(); ++i) {
        ll x = a[i]; if (x < 0) std::swap(hi, lo);
        hi = std::max(x, hi * x); lo = std::min(x, lo * x); best = std::max(best, hi);
    }
    return best;
}
```

```
best sum ending at each index of -2 1 -3 4 -1 2 1 -5 4: -2 1 -2 4 3 5 6 1 5
maximum subarray sum=6 from index 3 to 6
product -2 3 -4: (largest, smallest) ending at each index: (-2,-2) (3,-6) (24,-12)
maximum product=24
```

Common mistakes are measurable. On 100,000 random arrays of 8 numbers from -3 to 3, a Kadane that starts its best at 0 (so it never reports a negative sum) was wrong on 109 arrays, which is exactly the number of arrays that were all negative (109); and a product that tracks only the largest value was wrong on 70,927 (70.9%). Kadane on 10,000,000 elements took 12.0 / 9.7 ms, and the best subarray was 7,594,730 elements long.

## 4. The longest increasing subsequence

`f(i)` = the length of the longest strictly increasing subsequence **ending at** `i`; `f(i) = 1 + max f(j)` over `j < i` with `a[j] < a[i]`. That is O(n^2) (DP 01). It is the first example where the "obvious" state (the best among the first `i` elements) is wrong, and the repair is to name the end.

To get O(n log n), change what the array stores. Let `tails[k]` be the **smallest possible last value** of an increasing subsequence of length `k + 1`. Two facts make this work:

- `tails` is always **strictly increasing**. (If a subsequence of length `k + 1` ends in `v`, dropping its last element gives one of length `k` ending in something smaller than `v`.)
- Processing a new value `x`: find the first `tails[k] >= x`. Then `x` can end a subsequence of length `k + 1` (it extends one of length `k`, whose last value `tails[k-1] < x`), and it is no larger than the old `tails[k]`, so it **improves** that entry. If no tail is `>= x`, `x` is larger than all of them and extends the longest subsequence by one.

Because `tails` is sorted, "the first entry `>= x`" is a binary search. The answer is the length of `tails`. For non-strict (non-decreasing) subsequences use the first entry `> x` instead.

```cpp
// Longest strictly increasing subsequence. O(n^2): f(i) = longest ending at i (DP 01).
// O(n log n): tails[k] = the SMALLEST possible last value of an increasing subsequence of length k + 1. tails is always sorted, so a new
// value x either extends the longest (x is larger than every tail) or lowers one tail: the first tail >= x is replaced by x.
int lis_nlogn(const std::vector<int>& a, bool strict = true) {
    std::vector<int> tails;
    for (int x : a) {
        auto it = strict ? std::lower_bound(tails.begin(), tails.end(), x) : std::upper_bound(tails.begin(), tails.end(), x);
        if (it == tails.end()) tails.push_back(x); else *it = x;
    }
    return (int)tails.size();
}
// One longest subsequence (indices), by remembering for each element the end of the subsequence it extended.
std::vector<int> lis_sequence(const std::vector<int>& a) {
    std::vector<int> tail_idx, parent(a.size(), -1);
    for (int i = 0; i < (int)a.size(); ++i) {
        int lo = 0, hi = (int)tail_idx.size();
        while (lo < hi) { int mid = (lo + hi) / 2; if (a[tail_idx[mid]] < a[i]) lo = mid + 1; else hi = mid; }
        if (lo > 0) parent[i] = tail_idx[lo - 1];
        if (lo == (int)tail_idx.size()) tail_idx.push_back(i); else tail_idx[lo] = i;
    }
    std::vector<int> seq; if (tail_idx.empty()) return seq;
    for (int i = tail_idx.back(); i >= 0; i = parent[i]) seq.push_back(i);
    std::reverse(seq.begin(), seq.end()); return seq;
}
// HOW MANY longest strictly increasing subsequences? Keep for each i the length len[i] and the count cnt[i] of the longest ones ending
// at i. If extending from j gives a longer length, replace the count; if the same length, add the counts.
ll count_lis(const std::vector<int>& a) {
    int n = (int)a.size(); std::vector<int> len(n, 1); std::vector<ll> cnt(n, 1); int best = 0; ll total = 0;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < i; ++j) if (a[j] < a[i]) { if (len[j] + 1 > len[i]) { len[i] = len[j] + 1; cnt[i] = cnt[j]; } else if (len[j] + 1 == len[i]) cnt[i] += cnt[j]; }
        if (len[i] > best) { best = len[i]; total = cnt[i]; } else if (len[i] == best) total += cnt[i];
    }
    return total;
}
```

```
tails after each element of 10 9 2 5 3 7 101 18:
  10 -> 10
  9 -> 9
  2 -> 2
  5 -> 2 5
  3 -> 2 3
  7 -> 2 3 7
  101 -> 2 3 7 101
  18 -> 2 3 7 18
one longest subsequence (values): 2 3 7 18
count of longest in 1 3 5 4 7: 2 (length 4)
```

A trap: **`tails` is not itself a subsequence of the input.** For `3, 4, 1` the array ends as `1, 4`, but `1` comes after `4`. It only records the best possible last values, which is why the length is right and the contents are not the answer. To recover a real subsequence, remember for each element which element it extended (`parent`) and keep the **index** at each length; walk back from the last tail. The count of longest subsequences needs the O(n^2) pair `(length, count)` per element: if extending from `j` gives a longer length, replace the count; if equal, add (the count can be done in O(n log n) with a Fenwick tree, which a later tutorial can use).

Measured: on 30,000 random values the O(n^2) table took 1402 / 1457 ms and the `tails` version 1.18 / 1.36 ms, both finding a longest subsequence of length 337 (the reconstruction found one of the same length). On 1,000,000 values the O(n log n) version took 45 / 49 ms (length 1,983).

## 5. Recognising the shape

| Signal | State | Transition reads | Time |
|---|---|---|---|
| Make a total from items; fewest, count, or reachable | `f(x)`, the amount | `f(x - c)` for each item | O(amount x items) |
| Split a sequence into valid pieces; count or decide | `f(i)`, the prefix | `f(j)` for earlier `j` within the piece length | O(n x piece length) |
| Best structure that must end at each position | `f(i)`, ending at `i` | `f(j)` for earlier `j`, or just `f(i-1)` | O(n^2), O(n), or O(n log n) |
| Count paths or orderings where order matters | Amount outside, items inside | Same as above | Same |
| Count multisets where order does not matter | Items outside, amount inside | Same as above | Same |
| A negative can flip the best into the worst | Keep both the best and the worst | Both previous values | O(n) |

## 6. Testing a one-dimensional DP

Write the brute force of the *same* definition (enumerate every combination, every split, every subset), compare on thousands of random small inputs, and then add pins that random inputs do not reach: the empty input, a single element, an all-negative array, a zero where it matters, a leading `'0'`, an amount that cannot be made, a word longer than the text, equal values for the strict and non-strict versions, and results that exceed the type without the modulus. For the larger sizes compare with an independent *formula* where one exists (the four-square theorem, the prefix-minimum form of Kadane, the Fibonacci form of ordered 1-and-2 sequences, a binomial) so the check does not share a mistake with the code.

## 7. What the tests check, and what they cannot

Of the 45 mutants, 42 were caught by at least one check (a wrong answer, a sanitizer report, a hang or a crash). Three survive, and each leaves every output the tests can see unchanged. The first run of this mutant set left four survivors; a pin for a maximum subarray that begins with a prefix of sum 0 (`2, -2, 3` must report indices 0 to 2) killed one. The reasons the other three stay:

- **`f[x - c] < f[x]` instead of `f[x - c] + 1 < f[x]` in the fewest-coins loop.** The two differ only when `f[x - c] + 1 == f[x]`, and then the assignment writes the same value.
- **Dropping the `break` once a split is found in word break.** The flag is already set; the loop only does more work.
- **Flipping `bound` so that the unbounded mode uses the bound and the bounded mode does not.** The bound only skips lengths longer than every word, which can never match, so the two modes give the same answers and differ only in speed (which the benchmark measures, not the tests).

The large inputs have no brute-force reference; they are checked against independent computations (the theorems and formulas above, the O(n^2) tables). What the tests cannot do is prove that the state models the problem: they compare the code with a brute force of the same definition, so a misreading of the problem statement would pass.

Mutants are chosen where a bug is plausible, not at random, so "42 of 45" is a statement about those edits and not a proof about the listings. Timings depend on the machine.

## 8. Common mistakes

1. **Swapping the coin and amount loops** and counting the wrong thing (combinations versus ordered sequences).
2. **Starting Kadane's best at 0,** which never returns a negative answer.
3. **Tracking only the maximum product.** A negative factor makes the minimum the new maximum.
4. **Forgetting that `"0"` cannot be read alone,** or that `"06"` is not a code, in decode ways.
5. **Unbounded substring scans** in word break: O(n^2) substring copies. Bound by the longest word, or use a trie.
6. **The wrong state for LIS:** "best among the first `i`" instead of "ending at `i`".
7. **Treating `tails` as the subsequence.** It holds the best last values, not a real subsequence.
8. **Strict versus non-strict:** `lower_bound` for strictly increasing, `upper_bound` for non-decreasing.
9. **Adding to "infinity"** in the fewest-coins table and overflowing.
10. **Missing `% MOD`** in counting DPs, invisible on small inputs.
11. **Not stating the empty case:** one decoding of the empty string, zero coins for amount 0, length 0 for an empty LIS.
12. **Reading the table index `x - c` before checking `c <= x`.**

## 9. Questions to answer out loud

1. Which loop is outside for combinations, and which for ordered sequences? Prove it in one sentence each.
2. Why does the fewest-coins DP succeed where greedy fails?
3. How do you decide the answer for perfect squares is at most 4?
4. Write the word-break recurrence and say why the inner loop is bounded.
5. List the cases of the decode-ways transition, including the two zero rules.
6. Why does Kadane drop a negative running sum? State the DP it is.
7. Why does the maximum product need both a largest and a smallest value?
8. State the invariant of the `tails` array and why it is sorted.
9. Why is binary search valid there, and how does strict versus non-strict change it?
10. Is `tails` an increasing subsequence of the input? Give an example.
11. How do you recover an actual longest subsequence?
12. How do you count the longest ones, and what goes wrong if the counts are added when the lengths differ?

## 10. Problems

### 10.1 On paper

**A.** Coin change: the fewest coins from `{1, 2, 5}` for 11; the number of combinations of `{2, 3, 5}` for 10; the number of ordered sequences of `{1, 3, 4}` for 5.

**B.** Fewest perfect squares for 43, for 48 and for 28.

**C.** Word break: can `"aaaaab"` be split with `{a, aa, aaa}`? Can `"abcd"` be split with `{a, abc, b, cd}`?

**D.** Decode ways for `"1201234"`, for `"2610"` and for `"301"`.

**E.** The maximum subarray sum of `3, -4, 5, -1, 2, -6, 4`; the maximum product subarray of `-1, 2, -3, -4`.

**F.** The longest strictly increasing subsequence of `3, 10, 2, 1, 20` and of `3, 2`; the number of longest ones in `1, 2, 4, 3, 5, 4, 7, 2`; the longest non-decreasing subsequence of `1, 2, 2, 3`.

**G.** Write the `tails` array after each element of `4, 10, 4, 3, 8, 9`. What is the final array and the answer?

### 10.2 On LeetCode

For each: say the state in words, then the transition, then the loop order, before you write code. Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | What to practise |
|---|---|---|
| Coin Change | 322 | Fewest coins (section 1) |
| Coin Change II | 518 | Combinations: coins outside |
| Combination Sum IV | 377 | Ordered sequences: amount outside (listed in the plan for the knapsack tutorial, same code) |
| Perfect Squares | 279 | Coin change with squares |
| Word Break | 139 | Prefix state with a length bound |
| Decode Ways | 91 | The zero rules |
| Maximum Subarray | 53 | Kadane |
| Maximum Product Subarray | 152 | Two values per end |
| Longest Increasing Subsequence | 300 | O(n^2) then O(n log n) |
| Number of Longest Increasing Subsequence | 673 | Length and count per element |

### 10.3 In your editor

**A.** Write the coin-change counts with a bitset or a rolling array for huge amounts, and time the three tables at amount 10^7.

**B.** Write word break with a trie instead of a hash set and compare with the bounded version on a long text and a large dictionary; then return all the splits (Word Break II) and measure how many there can be.

**C.** Implement the Fenwick-tree version of `count_lis` in O(n log n) and compare it with the O(n^2) version on 10^5 elements.

**D.** Write Kadane returning the shortest of the best subarrays, and a version for a circular array; test against brute force.

**E.** Implement the longest non-decreasing subsequence, the longest bitonic subsequence (up then down) and the minimum number of deletions to make an array sorted, from the pieces here.

**F.** Reproduce the perfect-squares table up to 10^6 and check Legendre's rule for the values that need 4.

### Answers

**A.** Fewest from `{1, 2, 5}` for 11: 3 coins (5 + 5 + 1). Combinations of `{2, 3, 5}` for 10: 4 (`5+5`, `5+3+2`, `3+3+2+2`, `2+2+2+2+2`). Ordered sequences of `{1, 3, 4}` for 5: 6, from `f = 1, 1, 1, 2, 4, 6` (the code returns the same).

**B.** 43 is 25 + 9 + 9, so 3. 48 is 16 + 16 + 16, so 3. 28 is `4 * 7`, which has the form `4^a (8b + 7)`, so it needs 4.

**C.** `"aaaaab"`: no. The final `b` is not a word and no prefix ends in a split that reaches it. `"abcd"` with `{a, abc, b, cd}`: yes, as `a`, `b`, `cd`.

**D.** `"1201234"`: 3 ways. `"2610"`: 2 ways (`2 6 10` and `26 10`). `"301"`: 0, because `30` is not a code and a lone `0` cannot be read.

**E.** `3, -4, 5, -1, 2, -6, 4`: the best is 6, from `5, -1, 2`. For the product of `-1, 2, -3, -4`: the best is 24, from `2, -3, -4`.

**F.** `3, 10, 2, 1, 20`: length 3. `3, 2`: length 1. Count of longest in `1, 2, 4, 3, 5, 4, 7, 2`: 3. Longest non-decreasing in `1, 2, 2, 3`: 4.

**G.** After `4`: `4`. After `10`: `4, 10`. After the second `4`: `4, 10` (the first tail `>= 4` is 4, replaced by 4). After `3`: `3, 10`. After `8`: `3, 8`. After `9`: `3, 8, 9`. The final array is `3, 8, 9`, so the length is 3.

## Cheat sheet

- **Three shapes:** amount is the state (coins, squares); prefix is the state (word break, decode); end is the state (Kadane, LIS).
- **Coins:** `f(x) = 1 + min f(x - c)`. Count combinations with **coins outside**, ordered sequences with **amount outside**.
- **Perfect squares:** coins are the squares; the answer is at most 4, and exactly 4 when `n = 4^a (8b + 7)`.
- **Word break:** `f(i) = OR (f(j) and s[j..i) is a word)`, with `j` bounded by the longest word.
- **Decode ways:** add `f(i-1)` if the digit is not `'0'`; add `f(i-2)` if the pair is 10 to 26 with a nonzero first digit.
- **Kadane:** `f(i) = max(a[i], f(i-1) + a[i])`. **Product:** keep the largest and the smallest ending here; a negative swaps them.
- **LIS:** O(n^2) `f(i)` ending at `i`; O(n log n) `tails[k]` = smallest last value of length `k + 1`, `lower_bound` for strict. `tails` is not a subsequence.
- **Count longest:** per element, `(length, count)`; replace on a longer length, add on an equal one.
- **Test** with brute force of the same definition plus independent formulas, plus pins for empty, zero, negative and overflow.

---

## Sources

- All counts, exit codes and timings are measured here (container, GCC 13.3, `-O2`, two runs). The code is my own implementation, tested as described in the opening.
- The recurrences for coin change, word break, decode ways, Kadane's algorithm, the longest increasing subsequence with the `tails` array, and Lagrange's four-square and Legendre's three-square theorems are standard; the statements here are my own wording. That there are 73,682 ways to make 200 from the eight euro coins is a well-known count, reproduced here by running the code.
- Problem numbers and titles in section 10, and the samples of problems 322, 518, 377, 279, 139, 91, 53, 152, 300 and 673 used in the tests, are LeetCode's, from memory; check a number before you rely on it.
