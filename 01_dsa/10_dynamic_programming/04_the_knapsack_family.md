# DSA 10.04: The Knapsack Family

*Part of the [Dynamic programming plan](00_plan.md) · Time: about 9 hours (5 reading, 4 problems) · You need: [DP fundamentals](01_dp_fundamentals.md) (state, transition, rolling arrays) and [One-dimensional DP](02_one_dimensional_dp.md) (coin change, where the loop order changed the question). There is no companion demo file: every structure is listed in this doc

A **knapsack** problem has items, each with a cost (a weight) and a benefit (a value), and a budget (the capacity). You choose items to get the most benefit without exceeding the budget. The state is one number, the capacity used so far: `f(c)` = the best value you can get with capacity `c`. That is a one-dimensional array, and the entire family comes from one decision: **in which direction do you sweep that array?**

- Sweep it **downward** and each item is used at most once (the 0/1 knapsack).
- Sweep it **upward** and each item may be used again and again (the unbounded knapsack).

Many problems that do not mention a knapsack are one: "can these numbers be split into two equal halves?", "how many ways to put plus and minus signs to reach a target?", "the most strings that fit within a budget of zeros and ones?". Once you see the capacity hiding in the statement, they are all the same loop.

By the end you can:

- write the 0/1 and unbounded knapsack in one array, explain exactly why the sweep direction decides which one you get, and show it on a table;
- recover the chosen items, not just the best value;
- turn subset sum, equal partition, target sum, minimum difference and "ones and zeroes" into the same loop;
- speed up bounded knapsack (an item with a limit on copies) by splitting the count into powers of two;
- speed up subset sum by 64 with machine words;
- say why `O(n * capacity)` is not truly polynomial, and what to try when the capacity is huge.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 47,317 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: the 0/1 knapsack and its item walk against every subset of the items (the chosen items checked for weight, value and order); the unbounded knapsack against plain recursion over copy counts and against a separate method (each item copied as many times as it fits, then solved as 0/1); subset sum against every subset and the bitset version against the byte version at every word boundary (targets 63, 64, 65, 127, 128, 129 and so on); partition against the minimum-difference definition; subset counts against enumeration (with zeros, which double the count); target sum against all sign assignments, and for sixty ones against the binomial coefficient C(60, 30) computed separately; the minimum difference against all splits; ones and zeroes against every subset of the strings; bounded knapsack, both the plain and the binary-splitting versions, against recursion over copy counts, against each other, and, with counts so large the limit never binds, against the unbounded answer; and pins for the cases random inputs rarely hit (capacity 0, an item heavier than the knapsack, an empty list, odd totals, zeros, a target that is too large or of the wrong parity, values near a billion). Then 54 hand-made mutants of the listings were run against the tests: **49 were caught and 5 are equivalent changes**; section 8 says which survived and what that means.

**Data and timing.** Instances are **synthetic** and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are **run 1 / run 2**, one measurement per row per run, and a claim is made only when it holds in both; they are this machine's, not yours. Counts and answers are identical in both runs.

---

## 1. The 0/1 and unbounded knapsack

Take items with weights `w[i]` (at least 1) and values `v[i]`, and a capacity `cap`. Let `f(c)` be the best total value that fits in capacity `c`; `f(c) = 0` to start. Processing one item at a time, the choice for item `i` is "use it or not":

```
f(c) = max( f(c),  f(c - w[i]) + v[i] )     for c from w[i] up to cap
```

The right-hand `f(c)` is the best without this item; `f(c - w[i]) + v[i]` is the best that leaves room for it and then takes it. The whole question is **which value of `f(c - w[i])` the right-hand side reads**: the one from before this item (the item is used at most once) or the one already updated by this item (it may be used again). The sweep decides:

- **Downward** (`c` from `cap` to `w[i]`): when `c` is updated, the smaller index `c - w[i]` has not been touched yet by this item. It still holds "the best without item `i`", so item `i` is counted at most once. That is the 0/1 knapsack.
- **Upward** (`c` from `w[i]` to `cap`): `c - w[i]` was updated a moment ago by this very item, so it may already contain a copy, and `f(c)` may add another. That is the unbounded knapsack.

This is the same lesson as the coin-change loops in the previous tutorial: *the order in which a single array is overwritten decides what each cell means*.

```cpp
// Knapsack with capacity cap.  Item i has weight w[i] >= 1 and value v[i].  State f(c) = the best value that fits in capacity c.
// 0/1 knapsack: every item may be used AT MOST ONCE.  Process the items one at a time and sweep the capacity DOWNWARD:
// f(c) = max(f(c), f(c - w) + v).  Downward means f(c - w) has not been touched by this item yet, so it still describes "without it".
ll knapsack01(const std::vector<int>& w, const std::vector<ll>& v, int cap) {
    std::vector<ll> f(cap + 1, 0);
    for (size_t i = 0; i < w.size(); ++i) for (int c = cap; c >= w[i]; --c) f[c] = std::max(f[c], f[c - w[i]] + v[i]);
    return f[cap];
}
// Unbounded knapsack: every item may be used ANY number of times.  The same recurrence with the sweep UPWARD, so f(c - w) has
// already been updated by this item and may include it again.  Upward is the only change.
ll knapsack_unbounded(const std::vector<int>& w, const std::vector<ll>& v, int cap) {
    std::vector<ll> f(cap + 1, 0);
    for (size_t i = 0; i < w.size(); ++i) for (int c = w[i]; c <= cap; ++c) f[c] = std::max(f[c], f[c - w[i]] + v[i]);
    return f[cap];
}
// The chosen items: keep the whole table g(i, c) = best value using only the first i items, then walk back from (n, cap).
// Item i-1 is in the answer exactly when g(i, c) differs from g(i-1, c).  Returns the value and the indices in increasing order.
struct Pick { ll value; std::vector<int> items; };
Pick knapsack01_items(const std::vector<int>& w, const std::vector<ll>& v, int cap) {
    int n = (int)w.size(); std::vector<std::vector<ll>> g(n + 1, std::vector<ll>(cap + 1, 0));
    for (int i = 1; i <= n; ++i) for (int c = 0; c <= cap; ++c) {
        g[i][c] = g[i - 1][c]; if (c >= w[i - 1]) g[i][c] = std::max(g[i][c], g[i - 1][c - w[i - 1]] + v[i - 1]);
    }
    Pick r; r.value = g[n][cap]; int c = cap;
    for (int i = n; i >= 1; --i) if (g[i][c] != g[i - 1][c]) { r.items.push_back(i - 1); c -= w[i - 1]; }
    std::reverse(r.items.begin(), r.items.end()); return r;
}
```

The trace below uses two items, `(weight 2, value 3)` and `(weight 3, value 4)`, and capacity 7. Watch the first item: with the downward sweep the array shows `3` everywhere from capacity 2 up (one copy); with the upward sweep it shows `3, 3, 6, 6, 9, 9` (copies stacking up).

```
items (weight, value): (2,3) (3,4); capacity 0..7
start:                  0 0 0 0 0 0 0 0
0/1 after (2,3):        0 0 3 3 3 3 3 3
0/1 after (3,4):        0 0 3 4 4 7 7 7
unbounded after (2,3):  0 0 3 3 6 6 9 9
unbounded after (3,4):  0 0 3 4 6 7 9 10
code: 0/1=7 unbounded=10
```

**Recovering the items.** One array forgets which choice produced each value, so to print the chosen items keep the whole table `g(i, c)` = the best value using only the first `i` items. Item `i - 1` is in the answer exactly when `g(i, c)` differs from `g(i - 1, c)`; then subtract its weight from `c` and continue upward in `i`.

```
best value using the first i items (rows), capacity 0..7 (columns); items (w,v): (1,1) (3,4) (4,5) (5,7)
  i=0: 0 0 0 0 0 0 0 0
  i=1: 0 1 1 1 1 1 1 1
  i=2: 0 1 1 4 5 5 5 5
  i=3: 0 1 1 4 5 6 6 9
  i=4: 0 1 1 4 5 7 8 9
value=9, chosen items (0-based): 1 2
```

The value is 9, from items 1 and 2 (weights 3 and 4, values 4 and 5), which fill capacity 7 exactly. Reading the table: row `i=3`, column 7 is 9 already, so the weight-5 item (value 7) is not needed; the last row only ties.

**Measured.** With 2000 items (weights 1 to 500, values 1 to 1000) and capacity 20,000, the table version used 40,022,001 cells and took 240 / 217 ms; the single array used 20,001 cells and took 20 / 20 ms. Both found the value 229,677, and the table's walk chose 353 items. The table was slower in both runs, and the difference is mostly memory traffic.

**The two directions give different answers, and a swap is visible almost always.** On 100,000 random instances (up to 6 items, capacity up to 15), sweeping the 0/1 problem upward gave the wrong answer on 73,503 of them (73.5%), which is exactly the share of instances where the unbounded answer differs from the 0/1 answer; sweeping the unbounded problem downward was wrong on the very same instances, because the two errors are mirror images. On 400 random items with capacity 4000 the two answers were 14,587 (0/1) and 356,000 (unbounded).

## 2. Subset sum and its relatives

If the "value" of an item is its weight and the only question is whether the knapsack can be filled **exactly**, the array holds booleans. `f(s)` = some subset of the numbers seen so far adds up to `s`. This is the **subset sum** problem; it is the 0/1 knapsack and it sweeps downward.

Four common problems reduce to it:

- **Equal partition:** can the numbers be split into two groups with equal sums? The total must be even, and some subset must reach half of it.
- **Counting subsets:** replace the booleans by counts: `f(s) += f(s - x)`. (The empty subset counts for sum 0, and a zero doubles every count.)
- **Target sum:** put `+` or `-` in front of every number; how many ways give a total equal to `target`? Let `P` be the numbers given a plus and `N` those given a minus. Then `P - N = target` and `P + N = S` (the total), so `P = (S + target) / 2`. The question becomes "count the subsets that sum to `P`". If `S + target` is odd, or the target is larger than `S` in size, there are no ways.
- **Minimum difference** (the "last stone weight II" problem): split the numbers into two groups with the smallest difference. The best group sums to the largest reachable sum `s` not above `S / 2`, and the difference is `S - 2s`.

**Ones and zeroes** has two budgets, a number of zeros and a number of ones. It is a knapsack with **two** capacities: the table has two dimensions, `f(i, j)` = the most strings using at most `i` zeros and `j` ones, and **both** loops run downward so each string is used once.

```cpp
// Subset sum: can some subset of nums (non-negative) add up to exactly target?  The knapsack with weight = value, asking only whether
// capacity "target" can be filled exactly.  f(s) = true if some subset sums to s.  Downward sweep: each number used at most once.
bool subset_sum(const std::vector<int>& nums, int target) {
    std::vector<char> f(target + 1, 0); f[0] = 1;
    for (int x : nums) for (int s = target; s >= x; --s) if (f[s - x]) f[s] = 1;
    return f[target];
}
// Partition into two subsets of equal sum: the total must be even, and some subset must reach half of it.
bool can_partition(const std::vector<int>& nums) {
    ll total = 0; for (int x : nums) total += x; if (total % 2) return false;
    return subset_sum(nums, (int)(total / 2));
}
// Count the subsets that sum to target (the empty subset counts for 0; zeros double the count).  The counts must fit in 64 bits.
ll count_subsets(const std::vector<int>& nums, int target) {
    if (target < 0) return 0; std::vector<ll> f(target + 1, 0); f[0] = 1;
    for (int x : nums) for (int s = target; s >= x; --s) f[s] += f[s - x];
    return f[target];
}
// Target sum: put + or - in front of every number; how many ways make the total equal target?  With P = the numbers given a plus and
// N = those given a minus:  P - N = target and P + N = S, so P = (S + target) / 2.  That is a subset-count problem; if S + target is
// odd or P is out of range there are no ways.
ll target_sum_ways(const std::vector<int>& nums, int target) {
    ll S = 0; for (int x : nums) S += x; ll t = S + target;
    if (t < 0 || t % 2 || std::abs((ll)target) > S) return 0;
    return count_subsets(nums, (int)(t / 2));
}
// Last stone weight II / minimum difference of two groups: split the numbers into two groups with the smallest |sum1 - sum2|.
// Find the largest reachable sum s <= S / 2; the answer is S - 2 s.
int min_subset_diff(const std::vector<int>& nums) {
    int S = 0; for (int x : nums) S += x; std::vector<char> f(S / 2 + 1, 0); f[0] = 1;
    for (int x : nums) for (int s = S / 2; s >= x; --s) if (f[s - x]) f[s] = 1;
    int best = 0; for (int s = S / 2; s >= 0; --s) if (f[s]) { best = s; break; }
    return S - 2 * best;
}
// Ones and zeroes: the largest subset of binary strings that uses at most m zeros and at most n ones in total.  A knapsack with TWO
// capacities, so the table has two dimensions and BOTH are swept downward.
int ones_and_zeroes(const std::vector<std::string>& strs, int m, int n) {
    std::vector<std::vector<int>> f(m + 1, std::vector<int>(n + 1, 0));
    for (auto& s : strs) {
        int z = (int)std::count(s.begin(), s.end(), '0'), o = (int)s.size() - z;
        for (int i = m; i >= z; --i) for (int j = n; j >= o; --j) f[i][j] = std::max(f[i][j], f[i - z][j - o] + 1);
    }
    return f[m][n];
}
```

```
sums up to 12 that some subset reaches, after each number of 3 34 4 12 5 2
  start : 0
  after 3: 0 3
  after 34: 0 3
  after 4: 0 3 4 7
  after 12: 0 3 4 7 12
  after 5: 0 3 4 5 7 8 9 12
  after 2: 0 2 3 4 5 6 7 8 9 10 11 12
subset_sum for 9: 1, for 30: 0
```

After the number 2 the reachable sums are everything from 0 to 12 except 1: the numbers 3, 34, 4, 12, 5, 2 cannot make 1, and every other small sum is reachable. The trace for target sum shows the counting version on `1 1 1 1 1` with target 3: the total is 5, the plus group sums to `(5 + 3) / 2 = 4`, and the number of ways to choose four of the five ones is 5.

```
1 1 1 1 1 with target 3: S=5, plus-group sum P=(5+3)/2=4; ways to reach sums 0..4 after each number
  start : 1 0 0 0 0
  after 1: 1 1 0 0 0
  after 2: 1 2 1 0 0
  after 3: 1 3 3 1 0
  after 4: 1 4 6 4 1
  after 5: 1 5 10 10 5
ways=5 (code gives 5)
```

Look at the last row of that trace: `1 5 10 10 5` are the binomial coefficients C(5, k), as they must be when every number is 1. The tests use the same fact at a larger size: sixty ones with target 0 need thirty plus signs, and the count is C(60, 30) = 118,264,581,564,861,424 (exact, and it still fits in 64 bits).

Each one of these has a plausible slip, and the mistakes are measured. Using half of an odd total (integer division) in the partition test said "yes" where the answer is "no" on 38,649 of 100,000 random instances (38.6%); forgetting the parity and range checks in target sum gave a wrong count on 30,844 (30.8%).

## 3. Bounded knapsack and binary splitting

Now item `i` may be used at most `cnt[i]` times. The direct way turns it into `cnt[i]` separate 0/1 items, so the work grows with the **sum of the counts**. A trick brings it down to the sum of the **logarithms** of the counts: **binary splitting**.

Replace `cnt` copies of an item by pieces of size `1, 2, 4, ..., 2^(k-1)` and one remainder, so that the pieces add up to exactly `cnt`. Each piece is one 0/1 item with `piece` times the weight and `piece` times the value. Any number of copies from 0 to `cnt` can be built by choosing a subset of the pieces, and no larger number can, so the answer is the same. For example, 13 copies become the pieces 1, 2, 4 and 6; the choices of a subset cover every number from 0 to 13 and nothing above.

```cpp
// Bounded knapsack: item i may be used at most cnt[i] times.  Plain way: turn it into cnt[i] separate 0/1 items.
ll knapsack_bounded_naive(const std::vector<int>& w, const std::vector<ll>& v, const std::vector<ll>& cnt, int cap) {
    std::vector<ll> f(cap + 1, 0);
    for (size_t i = 0; i < w.size(); ++i) for (ll k = 0; k < cnt[i]; ++k) for (int c = cap; c >= w[i]; --c) f[c] = std::max(f[c], f[c - w[i]] + v[i]);
    return f[cap];
}
// Binary splitting: cnt copies are replaced by the pieces 1, 2, 4, ..., 2^(k-1) and the remainder, each piece a 0/1 item of
// weight piece * w and value piece * v.  The pieces can make every number of copies from 0 to cnt, and no more, so the answer is
// the same with about log2(cnt) items instead of cnt.
ll knapsack_bounded_split(const std::vector<int>& w, const std::vector<ll>& v, const std::vector<ll>& cnt, int cap) {
    std::vector<ll> f(cap + 1, 0);
    for (size_t i = 0; i < w.size(); ++i) {
        ll left = cnt[i];
        for (ll piece = 1; left > 0; piece <<= 1) {
            ll take = std::min(piece, left); left -= take;
            ll pw = take * w[i]; if (pw > cap) continue;   // a piece heavier than the knapsack can never be used
            for (int c = cap; c >= (int)pw; --c) f[c] = std::max(f[c], f[c - (int)pw] + take * v[i]);
        }
    }
    return f[cap];
}
```

```
count 13 split into pieces: 1 2 4 6; every number of copies 0..13 reachable: yes; 14 reachable: no
count 10 split into pieces: 1 2 4 3; every number of copies 0..10 reachable: yes; 11 reachable: no
```

Measured: 60 items with counts between 200 and 1000 (36,461 copies in all) and capacity 20,000. The plain version loops over every copy; the split version has only 576 pieces. Both gave the value 369,956. The plain version took 363 / 397 ms and the split one 4.8 / 6.0 ms.

(There is also an O(n * cap) method that keeps, for each residue of the capacity modulo the weight, a sliding-window maximum with a monotonic queue, independent of the counts. It is in the exercises; the binary splitting is the one to write in an interview.)

## 4. Subset sum with machine words

For subset sum only the set of reachable sums matters, and a set of small integers fits in a bit array. Adding the number `x` means "every reachable sum `s` also makes `s + x`", which is the whole bit array shifted left by `x` and ORed into itself. One word holds 64 sums, so one shift-and-OR handles 64 cells at once; the time drops from `O(n * target)` to about `O(n * target / 64)`.

```cpp
// Subset sum with machine words: keep the set of reachable sums as a bit array.  Adding the number x means "every reachable sum s
// also makes s + x", which is the whole array shifted left by x, ORed into itself.  One shift handles 64 sums at once.
bool subset_sum_bitset(const std::vector<int>& nums, int target) {
    size_t W = (size_t)target / 64 + 1; std::vector<uint64_t> b(W, 0); b[0] = 1;
    for (int x : nums) {
        if (x == 0) continue;
        size_t ws = (size_t)x / 64; int bs = x % 64;
        for (size_t i = W; i-- > ws;) {   // from the top down, so every word read below index i has not been changed yet
            uint64_t val = b[i - ws] << bs;
            if (bs && i > ws) val |= b[i - ws - 1] >> (64 - bs);
            b[i] |= val;
        }
    }
    return (b[(size_t)target / 64] >> (target % 64)) & 1;
}
```

```
reachable sums 0..15 as bits, lowest sum first, for subsets of 3 5 7
  start  :1000000000000000
  after 3:1001000000000000
  after 5:1001010010000000
  after 7:1001010110101001
subset_sum_bitset and subset_sum agree with these bits for every target 0..15: yes
```

Two details are worth knowing. The loop over the words runs from the top down, so every word it reads has not yet been changed by this shift (the same rule as the downward sweep). And the shift by `x` is split into a whole-word part (`x / 64`) and a bit part (`x % 64`); the bit part also carries the bits that spill into the next word. The tests exercise targets right at the word boundaries because that is where a carry or an off-by-one shows.

Measured: 1000 numbers (all even, between 2 and about 2000) and the target 200,000: the byte-per-sum version took 62 / 64 ms and the bitset version 3.4 / 3.5 ms. Both answered "reachable".

## 5. Recognising a knapsack

| Signal in the statement | Reduction | Sweep |
|---|---|---|
| Pick items under a budget to maximise a value; each used once | `f(c)` best value | capacity downward |
| Same, but an item can be used any number of times | `f(c)` best value | capacity upward |
| An item can be used at most `k` times | binary pieces, then 0/1 | downward over pieces |
| Can some numbers add up exactly to `t` | boolean `f(s)` | downward |
| Split into two equal halves | subset sum for half the total | downward |
| Count the ways to reach a sum with each number once | counting `f(s)` | downward |
| Plus or minus in front of every number | count subsets for `(S + target) / 2` | downward |
| Split into two groups with the least difference | largest reachable sum up to half | downward |
| Two budgets at once | two-dimensional `f(i, j)` | both downward |
| Count combinations of unlimited items (coins) | counting `f(x)` | upward, items outside |

## 6. Pseudo-polynomial time, and what to do about it

The knapsack runs in `O(n * cap)`. That looks polynomial but it is not: the input describes `cap` with about `log2(cap)` bits, so the running time is exponential in the *size* of the number. It is called **pseudo-polynomial**. For capacities of a few million it is fine; for a capacity of 10^12 the array cannot exist. The decision version of the knapsack is NP-complete (this is a standard fact, from memory), so no polynomial method in the number of bits is known. What people do instead, depending on what is small:

- **Values small, weights huge:** swap the roles. Let `f(v)` = the least weight that reaches value `v`; the table is indexed by value and its size is `n` times the total value.
- **Few items (about 40 or fewer), huge numbers:** meet in the middle. Enumerate the subset sums of each half (2^20 each), sort them, and combine the halves with two pointers.
- **Approximation is acceptable:** round the values down and use the value-indexed table; this is the idea behind the standard approximation scheme.
- **Sums only (no values):** the bitset of section 4.

## 7. Testing a knapsack

Three kinds of independent checks:

1. **Another method.** Enumerate every subset (small `n`), or recurse over copy counts, or rewrite the problem in a different form (each item copied as many times as it fits, then solved as 0/1). None of these shares code with the sweep.
2. **Relations between the functions.** The unbounded answer is at least the 0/1 answer. Bounded with counts that never bind equals unbounded. The binary-split answer equals the plain bounded answer. The bitset answer equals the byte answer. A count of subsets with zeros must double with every zero.
3. **Pins.** Capacity 0, an empty list, an item heavier than the knapsack, an odd total, a target out of range, the published samples, and sizes around the 64-bit word boundaries.

For the item walk the test also checks the *chosen set itself*: its weight is within the capacity, its value equals the reported value, and the indices are strictly increasing. A value check alone would let a wrong set slip through.

## 8. What the tests check, and what they cannot

Of the 54 mutants, 49 were caught by at least one check (a wrong answer, a sanitizer report, a hang or a crash). Five survive, and each leaves every output the tests can see unchanged:

- **Dropping the `t < 0` guard in `target_sum_ways`.** `t = S + target` is negative only when `target < -S`, and then `|target| > S` already returns 0.
- **Dropping the `|target| > S` guard.** Then the plus-group sum `(S + target) / 2` exceeds the total `S`, no subset reaches it, and `count_subsets` returns 0 anyway.
- **Asking for `S - t / 2` instead of `t / 2`.** A subset with sum `P` has a complement with sum `S - P`, so the two counts are equal. The plus group and the minus group are interchangeable.
- **Using `t = S - target` instead of `S + target`.** Flipping every sign turns a solution for `target` into one for `-target`, so the counts are equal.
- **Removing the `x == 0` skip in the bitset.** A shift by 0 ORs each word into itself and changes nothing.

All five are redundancies or symmetries of the problem, not gaps in the tests: no input can tell the mutated code from the original. The large inputs have no brute-force reference; they are checked against independent computations (a binomial coefficient, the byte version against the bitset, the unbounded answer for counts that never bind). What the tests cannot do is prove that the state models the problem: they compare the code with a brute force of the same definition, so a misreading of the problem statement would pass.

Mutants are chosen where a bug is plausible, not at random, so "49 of 54" is a statement about those edits and not a proof about the listings. Timings depend on the machine.

## 9. Common mistakes

1. **The wrong sweep direction:** upward for 0/1 (an item is reused) or downward for unbounded (an item is used once). Section 1 measured how often the error shows.
2. **Capacity outside, items inside** with a single array: that computes the unbounded knapsack (an item can be reused at every capacity), not the 0/1 one.
3. **Using half of an odd total** in the partition test (integer division).
4. **Missing the parity and range checks** in target sum, then dividing `S + target` by 2 anyway.
5. **Forgetting that zeros double the count** when counting subsets; a loop that skips zeros undercounts.
6. **Reading the answer from `f(cap)` when "at most" and "exactly" differ.** With the array starting at all zeros, `f(c)` means "at most `c`". For "exactly `c`" start with `f(0) = 0` and the rest at minus infinity.
7. **Sweeping only one dimension downward** in the two-budget problem.
8. **Counting the copies instead of the pieces** in a bounded knapsack, so large counts are hopelessly slow.
9. **Not skipping a piece heavier than the knapsack** (it would index below zero).
10. **Overflow:** values near a billion times thousands of items need 64 bits.
11. **Walking back from the one-array version.** It has forgotten the choices; keep the table to print the items.
12. **Bitset carries:** forgetting the bits that spill into the next word, or sweeping the words upward.

## 10. Questions to answer out loud

1. State what `f(c)` means, and what one step of the recurrence says in words.
2. Why does a downward sweep use each item at most once, and an upward sweep any number of times? Use a table as the proof.
3. Why does the coin-combinations loop (coins outside, amount inside, upward) count unbounded use?
4. How do you recover the chosen items, and why can the one-array version not do it?
5. Reduce equal partition to subset sum. What if the total is odd?
6. Derive `P = (S + target) / 2` for the target-sum problem. When are there no ways?
7. Why are there two downward loops in "ones and zeroes"?
8. Describe binary splitting. Why do the pieces cover exactly the numbers from 0 to `cnt`?
9. How does the bitset shift-and-OR work, and why must the words be processed from the top down?
10. Why is `O(n * cap)` called pseudo-polynomial?
11. The capacity is 10^12 but there are only 30 items. What do you do?
12. The values are small and the weights are huge. What do you do?

## 11. Problems

### 11.1 On paper

**A.** The 0/1 knapsack with weights `2, 3, 4, 5`, values `3, 4, 5, 6` and capacity 5; the unbounded knapsack with weights `2, 3`, values `3, 5` and capacity 10.

**B.** Can `1, 5, 10, 6` be split into two equal halves? Can `2, 2, 3, 5`? Can `3, 34, 4, 12, 5, 2` make 15?

**C.** The number of ways to put signs in `1, 2, 3, 4, 5` to reach 3; in `1, 1, 2` to reach 2.

**D.** The minimum difference of two groups for `3, 9, 7, 3`; for `1, 6, 11, 5`.

**E.** Ones and zeroes: the strings `00, 01, 11` with at most 2 zeros and 2 ones. How many can you take?

**F.** Bounded knapsack: weights `2, 3`, values `4, 7`, at most `3, 2` copies, capacity 10. Also, split a count of 21 into binary pieces.

**G.** List the sums up to 12 that are reachable from subsets of `2, 3, 7`.

### 11.2 On LeetCode

For each: say what the capacity is, which sweep you need and why, before you write code. Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | What to practise |
|---|---|---|
| Partition Equal Subset Sum | 416 | Section 2: subset sum for half the total |
| Target Sum | 494 | The reduction to counting subsets |
| Last Stone Weight II | 1049 | Minimum difference |
| Ones and Zeroes | 474 | Two capacities |
| Coin Change II | 518 | Unbounded, counting, items outside (previous tutorial) |
| Combination Sum IV | 377 | Counting ordered sequences, capacity outside |
| Coin Change | 322 | Unbounded, fewest items |
| Perfect Squares | 279 | Unbounded with the squares as items |
| Partition to K Equal Sum Subsets | 698 | Beyond this loop: needs a bitmask (next area) |
| 0/1 Knapsack (any judge) | none | Section 1, with the items printed |

### 11.3 In your editor

**A.** Write the unbounded knapsack with the items printed (the table and the walk back). How do you know which item was last added at each capacity?

**B.** Write the bounded knapsack with the monotonic-queue method that handles each residue modulo the weight in one pass. Compare it with binary splitting on counts around 10^5.

**C.** Write the value-indexed knapsack (`f(v)` = the least weight reaching value `v`) and use it where the capacity is huge but the total value is small.

**D.** Write meet in the middle for subset sum with 40 numbers of size up to 10^12 and check it against the bitset on a smaller version.

**E.** Count the subsets of a list that sum to a target modulo a prime, with n = 200 and sums up to 10^5, and compare with the exact count on a small case.

**F.** Reproduce the bitset subset sum with `std::bitset` of a fixed size, and compare its speed with the hand-written word version.

### Answers

**A.** The 0/1 knapsack with capacity 5: 7 (take the items of weights 2 and 3, values 3 and 4). The unbounded knapsack with capacity 10: 16 (two items of weight 2 and two of weight 3: values `3 + 3 + 5 + 5`).

**B.** `1, 5, 10, 6`: yes (the total is 22, half is 11, and `1 + 10 = 11`). `2, 2, 3, 5`: no (the total 12 gives half 6, but no subset sums to 6). `3, 34, 4, 12, 5, 2` can make 15: yes (`3 + 12`).

**C.** For `1, 2, 3, 4, 5` and target 3: 3 ways. For `1, 1, 2` and target 2: 2 ways.

**D.** `3, 9, 7, 3`: 2. `1, 6, 11, 5`: 1.

**E.** 2 strings (`00` and `11`).

**F.** The best value is 22 (all three copies of the first item and one of the second give weight 9 and value 19; two copies of the first and two of the second give weight 10 and value 22). A count of 21 splits into the pieces `1, 2, 4, 8, 6`.

**G.** The reachable sums up to 12 are 0, 2, 3, 5, 7, 9, 10 and 12.

## Cheat sheet

- **State:** `f(c)` = the best value with capacity `c`. Items outside, capacity inside.
- **0/1:** capacity **downward**. **Unbounded:** capacity **upward**. The direction decides whether `f(c - w)` includes the current item.
- **Print the items:** keep the table `g(i, c)`; item `i - 1` is chosen when `g(i, c) != g(i - 1, c)`.
- **Subset sum:** booleans, downward. **Partition:** total even and half reachable. **Count subsets:** `f[s] += f[s - x]`, downward.
- **Target sum:** `P = (S + target) / 2`, no ways if `S + target` is odd or `|target| > S`.
- **Minimum difference:** largest reachable `s <= S / 2`, answer `S - 2s`.
- **Two budgets:** a two-dimensional table, both loops downward.
- **Bounded:** split a count into `1, 2, 4, ..., remainder` and run 0/1.
- **Bitset:** shift by `x` and OR, words from the top down.
- **Pseudo-polynomial:** `O(n * cap)` is exponential in the number of bits of `cap`; use values, meet in the middle or approximation when `cap` is huge.
- **Test** with another method, with relations between functions, and with pins.

---

## Sources

- All counts, exit codes and timings are measured here (container, GCC 13.3, `-O2`, two runs). The code is my own implementation, tested as described in the opening.
- The knapsack recurrences, the reductions of partition and target sum to subset sum, binary splitting of bounded items, and the bitset subset sum are standard; the statements here are my own wording. That the knapsack decision problem is NP-complete, that the value-indexed table gives an approximation scheme, that meet in the middle works for about 40 items, and the monotonic-queue method for bounded knapsack are from memory of the standard accounts; they are described, not proved or measured here.
- Problem numbers and titles in section 11, and the samples of problems 416, 494, 1049, 474, 377 and 518 used in the tests, are LeetCode's, from memory; check a number before you rely on it.
