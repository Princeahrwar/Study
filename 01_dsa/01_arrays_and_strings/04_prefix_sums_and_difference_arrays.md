# DSA 01.04: Prefix Sums and Difference Arrays

*Part of the [Arrays and Strings plan](00_plan.md) · Time: about 4 hours (1.5 reading, 2.5 problems) · You need: tutorials [01](01_complexity_and_arrays_in_memory.md), [02](02_two_pointers.md) and [03](03_sliding_window.md); section 4 of tutorial 03 is the reason this one exists · Companion code: [`04_prefix_sums_demo.cpp`](04_prefix_sums_demo.cpp)*

By the end of this tutorial you can:

- answer any number of range-sum queries in O(1) each after one O(n) pass, with no off-by-one errors;
- count or find subarrays with a given sum when the numbers can be **negative**, and adapt the same idea to XOR, remainders and 0/1 balance;
- build and query a 2D prefix table (the sum of any rectangle in O(1));
- apply many range updates with a **difference array**, in one and two dimensions;
- say which operations the trick works for, and what to use when it does not.

Every function in this tutorial is in the companion file, which compiles with `-Wall -Wextra -fsanitize=address,undefined` and checks each function against a brute force on random inputs.

---

## 1. The idea

You are given an array `a` of `n` numbers and `Q` queries of the form "what is `a[l] + ... + a[r]`?" Adding up each range costs O(n) per query, so O(n × Q) in all. For n = Q = 10⁵ that is 10¹⁰ steps, far too slow.

But the queries overlap heavily, so do the shared work once. Let `P[i]` be the sum of the **first `i` elements**, with `P[0] = 0` (the sum of nothing).

```text
 i:           0   1   2   3   4   5   6   7   8
 P[i]:        0   3   4   8   9  14  23  25  31      P[i] = a[0] + ... + a[i-1]
 a[i-1]:          3   1   4   1   5   9   2   6      the element added at each step
```

`P[i]` covers `a[0..i-1]`. The sum of `a[l..r]` is everything up to and including `r`, minus everything before `l`:

> **sum(a[l..r]) = P[r + 1] − P[l]**

Check: `a[2..5] = 4 + 1 + 5 + 9 = 19`, and `P[6] − P[2] = 23 − 4 = 19`.

```cpp
std::vector<long long> buildPrefix(const std::vector<int>& a) {
    std::vector<long long> p(a.size() + 1, 0);       // p[0] = 0: the empty prefix
    for (size_t i = 0; i < a.size(); ++i) p[i + 1] = p[i] + a[i];
    return p;
}
```

```cpp
// Sum of a[l..r], both ends included (0 <= l <= r < n).
long long rangeSum(const std::vector<long long>& p, int l, int r) {
    return p[r + 1] - p[l];
}
```

| | Time | Space |
|---|---|---|
| Build `P` | O(n) | O(n) |
| One query | **O(1)** | none |
| Q queries in all | O(n + Q), against O(n × Q) | |

Three details that decide whether you get it right:

1. **The array has `n + 1` entries and starts with 0.** Then `l = 0` needs no special case. Without the leading zero you end up writing `l == 0 ? P[r] : P[r] − P[l − 1]`, which is the classic source of off-by-one bugs. Always use `P[r + 1] − P[l]`.
2. **Use `long long`.** A hundred thousand values of a billion add up to 10¹⁴, far past the 2.1 × 10⁹ limit of a 32-bit `int`.
3. **The array must not change.** If `a[i]` changes, every `P[j]` with `j > i` changes with it, so one update costs O(n). For updates mixed with queries use a Fenwick tree or segment tree (Trees 20 and 21), which do both in O(log n).

The standard library has `std::partial_sum`, which fills `P[1..n]`; you still supply `P[0]` yourself. The same idea counts things too: to answer "how many `a`s are in `s[l..r]`?" keep one prefix array per letter, `cnt[c][i]` = occurrences of `c` in the first `i` characters, and subtract.

---

## 2. Subarray sum equals K, with negative numbers

Tutorial 03 ended with a warning: a sliding window needs the sum to move in one direction when you shrink, which fails with negative numbers. Prefix sums give a different view that does not care about signs.

The sum of `a[l..r]` is `P[r + 1] − P[l]`. We want that to equal `K`, so we want

> **P[l] = P[r + 1] − K**

Count the subarrays that sum to `K` = count the pairs of prefixes `(l, r + 1)` where `l` is earlier and the two prefix values differ by `K`. That is the **two-sum** problem on the prefix array, which a hash map solves. Scan left to right; for each new prefix value ask the map "how many earlier prefixes equal this value minus `K`?", then add the new prefix to the map.

```cpp
// Number of non-empty subarrays whose sum is exactly k. Negative numbers allowed.
long long countSubarraysWithSum(const std::vector<int>& a, long long k) {
    std::unordered_map<long long, long long> seen;   // prefix value -> how many earlier prefixes had it
    seen[0] = 1;                                     // the empty prefix, so subarrays that start at index 0 are found
    long long prefix = 0, count = 0;
    for (int x : a) {
        prefix += x;
        auto it = seen.find(prefix - k);             // look up BEFORE inserting the current prefix
        if (it != seen.end()) count += it->second;
        ++seen[prefix];
    }
    return count;
}
```

Trace on `{3, 4, 7, 2, −3, 1, 4, 2}` with `K = 7`. The map starts as `{0: 1}`.

| Element | Prefix | Need (prefix − 7) | Earlier prefixes equal to need | Total | The subarray found |
|---|---|---|---|---|---|
| 3 | 3 | −4 | 0 | 0 | |
| 4 | 7 | 0 | 1 (the empty prefix) | 1 | `3, 4` |
| 7 | 14 | 7 | 1 | 2 | `7` |
| 2 | 16 | 9 | 0 | 2 | |
| −3 | 13 | 6 | 0 | 2 | |
| 1 | 14 | 7 | 1 | 3 | `7, 2, −3, 1` |
| 4 | 18 | 11 | 0 | 3 | |
| 2 | 20 | 13 | 1 | 4 | `1, 4, 2` |

The answer is 4. Three details matter:

1. **Seed the map with `{0: 1}`.** That is the empty prefix `P[0]`. Without it, subarrays that start at index 0 are never counted (the `3, 4` row above finds its match only through this entry).
2. **Look up before you insert.** If you insert the current prefix first, then with `K = 0` every prefix matches itself and you count empty subarrays. With `{0, 0, 0}` and `K = 0` the right answer is 6, and only the look-up-first order gives it.
3. **Store counts, not a yes/no.** The same prefix value can occur many times, and each occurrence is a different starting point.

**Cost.** O(n) expected time and O(n) space, because in the worst case all `n + 1` prefix values are different. The hash map is the weak spot: `std::unordered_map` is O(1) on average but can degrade to O(n) per operation when many keys collide (the hashing area covers how and why), and each insert allocates a node. Call `seen.reserve(n)` when you know the size. If you need guaranteed time, sort the prefix values and replace them by their ranks, which costs O(n log n).

Window or prefix sums? If all numbers are non-negative a window does the job in O(1) space. As soon as a negative number is allowed, use prefix sums and a map.

### 2.1 Longest, shortest, or "does one exist?"

Counting needs *how many* earlier prefixes match. The other questions need different things stored.

| Question | The map stores | If the prefix repeats |
|---|---|---|
| How many subarrays? | count of each prefix | add one |
| Does one exist? | a set of prefixes | nothing |
| **Longest** subarray | the **first** index of each prefix | keep the old one |
| **Shortest** subarray | the **latest** index of each prefix | overwrite |

The earliest matching start gives the longest subarray and the latest one gives the shortest, which is why the two answers differ in one line:

```cpp
// Length of the longest subarray whose sum is exactly k (0 if there is none).
int longestSubarrayWithSum(const std::vector<int>& a, long long k) {
    std::unordered_map<long long, int> first;        // prefix value -> the FIRST index where it occurred
    first[0] = -1;                                   // the empty prefix sits just before index 0
    long long prefix = 0;
    int best = 0;
    for (int i = 0; i < (int)a.size(); ++i) {
        prefix += a[i];
        auto it = first.find(prefix - k);
        if (it != first.end()) best = std::max(best, i - it->second);
        first.emplace(prefix, i);                    // emplace keeps the earliest index if the key already exists
    }
    return best;
}
```

Here the empty prefix sits at index −1 (just before the first element), so a subarray from `j + 1` to `i` has length `i − j`. `emplace` leaves an existing key alone, which keeps the earliest index. For the shortest version change `emplace` to an assignment (`last[prefix] = i`) and take the minimum length instead of the maximum; the companion file has it.

---

## 3. The same trick with other "sums"

Section 2 used one fact: the value of a range is a *subtraction* of two prefix values, so equal-looking questions turn into "find two prefixes with a given relationship". Three common variations follow.

### 3.1 Equal numbers of 0s and 1s

Replace every 0 by −1. A subarray has equally many 0s and 1s exactly when its sum is 0, and we want the longest one. So this is "longest subarray with sum 0", and the running prefix is just `ones − zeros`, called the **balance**.

```cpp
// LeetCode 525: longest subarray with equally many 0s and 1s.
int findMaxLength(const std::vector<int>& nums) {
    std::unordered_map<int, int> first{{0, -1}};     // balance (ones minus zeros) -> first index
    int balance = 0, best = 0;
    for (int i = 0; i < (int)nums.size(); ++i) {
        balance += nums[i] == 1 ? 1 : -1;
        auto it = first.find(balance);
        if (it == first.end()) first[balance] = i;
        else best = std::max(best, i - it->second);
    }
    return best;
}
```

Trace on `{0, 1, 1, 0, 1, 1, 1, 0}`. The balances are −1, 0, 1, 0, 1, 2, 3, 2. A balance seen for the first time is stored; a balance seen again closes a balanced subarray from just after its first occurrence.

| i | nums[i] | balance | action | best |
|---|---|---|---|---|
| 0 | 0 | −1 | new, store index 0 | 0 |
| 1 | 1 | 0 | seen at −1 (the empty prefix): length 2 | 2 |
| 2 | 1 | 1 | new, store index 2 | 2 |
| 3 | 0 | 0 | seen at −1: length 4 | **4** |
| 4 | 1 | 1 | seen at 2: length 2 | 4 |
| 5 | 1 | 2 | new, store index 5 | 4 |
| 6 | 1 | 3 | new, store index 6 | 4 |
| 7 | 0 | 2 | seen at 5: length 2 | 4 |

The answer is 4 (`0, 1, 1, 0`).

### 3.2 Sum divisible by K

`(P[r + 1] − P[l]) % K == 0` exactly when `P[r + 1] % K == P[l] % K`: two prefixes with the same remainder. There are only `K` possible remainders, so a plain array replaces the map. Every earlier prefix with the same remainder pairs with the current one.

```cpp
// LeetCode 974: number of subarrays whose sum is divisible by k.
int subarraysDivByK(const std::vector<int>& nums, int k) {
    std::vector<int> count(k, 0);                    // count[r] = how many prefixes so far have remainder r
    count[0] = 1;                                    // the empty prefix
    long long prefix = 0;
    int result = 0;
    for (int x : nums) {
        prefix = ((prefix + x) % k + k) % k;         // C++ % can be negative: shift into 0..k-1
        result += count[prefix]++;                   // every earlier prefix with the same remainder pairs with this one
    }
    return result;
}
```

Trace on `{4, 5, 0, −2, −3, 1}` with `K = 5` (the table shows the prefix remainder after each element):

| Element | Remainder | Earlier prefixes with it | Total |
|---|---|---|---|
| 4 | 4 | 0 | 0 |
| 5 | 4 | 1 | 1 |
| 0 | 4 | 2 | 3 |
| −2 | 2 | 0 | 3 |
| −3 | 4 | 3 | 6 |
| 1 | 0 | 1 (the empty prefix) | **7** |

**The negative remainder trap.** In C++, `-1 % 5` is `-1`, not `4` (since C++11 the result has the sign of the dividend). A prefix remainder that comes out negative lands in the wrong bucket, and `count[-1]` is out of bounds. The expression `((x % k) + k) % k` always gives a value in `0..k−1`. The same trap appears in every "modulo" interview problem.

### 3.3 XOR

XOR undoes itself (`v ^ v = 0`), so it behaves like subtraction here: if `X[i]` is the XOR of the first `i` elements, the XOR of `a[l..r]` is `X[r + 1] ^ X[l]`. Counting the subarrays with XOR equal to `K` is section 2 with `^` in place of `−`:

```cpp
// Number of subarrays whose XOR is exactly k.
long long countSubarraysWithXor(const std::vector<unsigned>& a, unsigned k) {
    std::unordered_map<unsigned, long long> seen;
    seen[0] = 1;
    unsigned prefix = 0;
    long long count = 0;
    for (unsigned x : a) {
        prefix ^= x;
        auto it = seen.find(prefix ^ k);             // x[r + 1] ^ x[l] == k  <=>  x[l] == x[r + 1] ^ k
        if (it != seen.end()) count += it->second;
        ++seen[prefix];
    }
    return count;
}
```

### 3.4 What this works for, and what it does not

The trick needs an **inverse**: a way to take the prefix before `l` back out of the prefix up to `r`.

| Operation | Has an inverse? | Prefix trick? |
|---|---|---|
| Sum, count, frequency | yes (subtract) | yes |
| XOR | yes (itself) | yes |
| Product | only if no element is zero (divide, or use a modular inverse) | rarely |
| **Minimum, maximum** | **no**: knowing the minimum of the first 5 elements and of the first 8 does not give the minimum of elements 6 to 8 | no |
| GCD | no, for the same reason | no |

For range minimum, maximum and GCD use a **sparse table** (static array, Trees 19), a segment tree (updates allowed, Trees 21) or a monotonic deque (sliding windows, Stacks and Queues 03).

---

## 4. 2D prefix sums

Now `a` is an `m × n` grid and each query asks for the sum of a **rectangle**. Let `P[i][j]` be the sum of the top-left block with `i` rows and `j` columns. As in 1D the table has an extra row and an extra column of zeros, so it is `(m + 1) × (n + 1)`.

**Building it.** To get `P[i + 1][j + 1]`, take the block above, the block to the left, and the cell itself. The block above and the block to the left both contain the top-left block, so subtract that once:

> P[i + 1][j + 1] = a[i][j] + P[i][j + 1] + P[i + 1][j] − P[i][j]

```text
   regions:  A | B        P[i][j]     = A
             --+--        P[i][j+1]   = A + B
             C | D        P[i+1][j]   = A + C
                          P[i+1][j+1] = A + B + C + D     (D is the cell a[i][j])
```

`(A + B) + (A + C) − A + D` is `A + B + C + D`, as required.

**Querying it.** For the rectangle with corners `(r1, c1)` and `(r2, c2)`, both included, start with the big block ending at `(r2, c2)`, remove the strip above the rectangle and the strip to its left, and add back the corner that was removed twice:

> sum = P[r2 + 1][c2 + 1] − P[r1][c2 + 1] − P[r2 + 1][c1] + P[r1][c1]

```cpp
struct Prefix2D {
    std::vector<std::vector<long long>> p;           // (m + 1) x (n + 1); p[i][j] = sum of the top-left i x j block
    explicit Prefix2D(const std::vector<std::vector<int>>& a) {
        int m = (int)a.size(), n = m ? (int)a[0].size() : 0;
        p.assign(m + 1, std::vector<long long>(n + 1, 0));
        for (int i = 0; i < m; ++i)
            for (int j = 0; j < n; ++j)
                p[i + 1][j + 1] = a[i][j] + p[i][j + 1] + p[i + 1][j] - p[i][j];
    }
    // Sum of the rectangle with corners (r1, c1) and (r2, c2), both included.
    long long sum(int r1, int c1, int r2, int c2) const {
        return p[r2 + 1][c2 + 1] - p[r1][c2 + 1] - p[r2 + 1][c1] + p[r1][c1];
    }
};
```

Worked example. The grid and its prefix table:

```text
   a:  1  2  3  4            P:  0   0   0   0   0
       5  6  7  8                0   1   3   6  10
       9 10 11 12                0   6  14  24  36
                                 0  15  33  54  78
```

Query rows 1 to 2, columns 1 to 2 (the cells 6, 7, 10, 11, sum 34):

`P[3][3] − P[1][3] − P[3][1] + P[1][1] = 54 − 6 − 15 + 1 = 34`

Cost: O(m × n) to build, O(1) per rectangle. Two uses worth knowing:

- **Submatrices with a given sum.** Fix a pair of rows (O(m²) pairs), add up the columns between them to get a 1D array, and apply section 2 to it. Total O(m² × n) instead of O(m² × n²). (LeetCode 1074.)
- **Integral images.** Computer vision stores exactly this table so that a box filter of any size costs four lookups per pixel.

---

## 5. Difference arrays

Now the reverse problem. You start with `n` zeros and receive `q` updates, each saying "add `v` to every element of `a[l..r]`". You only look at the array at the end. Doing the updates one by one costs O(n) each, O(n × q) in all.

The **difference array** stores how much each element differs from its left neighbour: `D[i] = a[i] − a[i − 1]`, with `D[0] = a[0]`. Adding `v` to the whole range `l..r` changes only two differences: it creates a step **up** by `v` at `l`, and a step **down** by `v` just after `r`. Everything strictly inside the range keeps the same difference to its neighbour, because both moved. So:

> **D[l] += v;  D[r + 1] −= v** (two operations, whatever the length of the range)

When all updates are in, a prefix sum of `D` gives back `a`. Prefix sums and difference arrays are inverse operations, like integral and derivative.

```text
 add +2 to a[1..3] on six zeros:

 index:     0   1   2   3   4   5   6
 D:         0  +2   0   0  -2   0   0        step up at 1, step down at 4 (= r + 1)
 prefix:    0   2   2   2   0   0            the array a
```

```cpp
// n slots start at 0. Each update {l, r, v} adds v to slots l..r (both included).
std::vector<long long> applyRangeAdds(int n, const std::vector<std::array<int, 3>>& updates) {
    std::vector<long long> d(n + 1, 0);              // one extra slot, so r + 1 never needs a bounds check
    for (const auto& [l, r, v] : updates) {
        d[l] += v;
        d[r + 1] -= v;
    }
    for (int i = 1; i < n; ++i) d[i] += d[i - 1];    // a prefix sum turns the differences back into values
    d.pop_back();
    return d;
}
```

Worked example: `n = 6`, updates `[1,3] += 2`, `[2,5] += 3`, `[0,1] += 1`.

| After | D (indices 0 to 6) |
|---|---|
| start | 0, 0, 0, 0, 0, 0, 0 |
| `[1,3] += 2` | 0, 2, 0, 0, −2, 0, 0 |
| `[2,5] += 3` | 0, 2, 3, 0, −2, 0, −3 |
| `[0,1] += 1` | 1, 2, 2, 0, −2, 0, −3 |

The prefix sums of the last row are 1, 3, 5, 5, 3, 3: slot 3 was in the first two updates (2 + 3 = 5), slot 1 in the first and third (2 + 1 = 3). Cost: O(n + q) instead of O(n × q). The extra slot `D[n]` exists only so that `r + 1` never needs a bounds check.

The standard library already has the pair: `std::adjacent_difference` builds `D` from `a`, and `std::partial_sum` turns it back. The companion file checks that round trip.

**The limit:** you may only read the array **after all updates**. If updates and reads are mixed ("add to a range, then query a point, then add again"), a difference array no longer works and you need a Fenwick tree or a lazy segment tree (Trees 20 and 22).

### 5.1 The sweep-line view: Car Pooling

A trip `(passengers, from, to)` is two events: `+passengers` at `from` and `−passengers` at `to`. The running total is the load on the car at each location, which is just the prefix sum of a difference array.

```cpp
// LeetCode 1094: can one car with this capacity do all trips {passengers, from, to}?
bool carPooling(const std::vector<std::vector<int>>& trips, int capacity) {
    std::vector<int> delta(1002, 0);                 // locations are 0..1000
    for (const auto& t : trips) {
        delta[t[1]] += t[0];                         // passengers get on at t[1] ...
        delta[t[2]] -= t[0];                         // ... and off at t[2]
    }
    int load = 0;
    for (int d : delta) {
        load += d;
        if (load > capacity) return false;
    }
    return true;
}
```

A passenger who leaves at the stop where another boards is handled correctly, because both changes land in the same slot before the load is checked. When locations are huge (10⁹) you cannot allocate an array; sort the events by position instead (Greedy and Intervals 02). "Minimum meeting rooms" and "maximum overlapping intervals" are the same idea.

### 5.2 Difference arrays in 2D

To add `v` to a whole rectangle, change four corners instead of the whole block, using the same inclusion-exclusion shape as the 2D query:

```cpp
// Add v to every cell of the rectangle (r1, c1)..(r2, c2) on a difference grid.
void addRect(std::vector<std::vector<long long>>& d, int r1, int c1, int r2, int c2, long long v) {
    d[r1][c1] += v;
    d[r1][c2 + 1] -= v;
    d[r2 + 1][c1] -= v;
    d[r2 + 1][c2 + 1] += v;
}
```

When all updates are in, take the 2D prefix sum of the grid: `out[i][j] = d[i][j] + out[i−1][j] + out[i][j−1] − out[i−1][j−1]` (terms that fall outside the grid count as 0). The grid needs one extra row and column, as in 1D.

---

## 6. Prefix sums and binary search: picking by weight

*Question:* pick an index `i` at random, with probability `w[i] / (w[0] + ... + w[n−1])`.

Lay the weights end to end on a line. With weights `{1, 3, 2}` the line has length 6 and the prefix sums `{1, 4, 6}` are the right-hand ends of the three segments. Choose a random integer `target` from 1 to 6; the segment that contains it is the first one whose prefix sum is **at least** `target`. The prefix array is sorted, so binary search finds it (`std::lower_bound` returns the first element not less than `target`).

| target | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| index | 0 | 1 | 1 | 1 | 2 | 2 |

Weight 1 gets one target, weight 3 gets three, weight 2 gets two, as it should.

```cpp
// LeetCode 528: pick index i with probability w[i] / sum(w).
int indexForTarget(const std::vector<long long>& prefix, long long target) {
    // prefix[i] = w[0] + ... + w[i]; target is in 1..prefix.back()
    return int(std::lower_bound(prefix.begin(), prefix.end(), target) - prefix.begin());
}
```

```cpp
class WeightedPicker {
    std::vector<long long> prefix_;
    std::mt19937_64 rng_;
public:
    WeightedPicker(const std::vector<int>& w, unsigned seed) : rng_(seed) {
        long long s = 0;
        for (int x : w) {
            s += x;
            prefix_.push_back(s);
        }
    }
    int pick() {
        long long target = std::uniform_int_distribution<long long>(1, prefix_.back())(rng_);
        return indexForTarget(prefix_, target);
    }
    const std::vector<long long>& prefix() const { return prefix_; }
};
```

O(n) to build, O(log n) per pick. The same "cumulative total, then binary search" idea finds a percentile in a histogram (section 7). An O(1) method (the alias method) comes in the probability tutorial of Bits and Math.

---

## 7. Prefix sums in systems you already use

- **Cumulative counters.** An interface's `rx_bytes`, a Prometheus counter or a hardware performance counter is the running total of everything that has happened so far: a prefix sum. The traffic in a window is a range sum, `after − before`. With unsigned arithmetic the subtraction is correct even if the counter wrapped once in between:

```cpp
// A cumulative 32-bit counter is a prefix sum. The difference of two readings is a range sum,
// and unsigned subtraction is correct across one wrap-around.
std::uint32_t bytesBetween(std::uint32_t before, std::uint32_t after) {
    return after - before;
}
```

  If it wrapped twice between two readings the information is lost, so sample faster than the wrap time. A 32-bit byte counter at 10 Gbit/s (1.25 GB/s) wraps every 2³² / 1.25 × 10⁹ ≈ **3.4 seconds**.
- **Counting sort and graph layouts.** The start offset of each bucket is the prefix sum of the bucket sizes. Counting sort (Binary Search and Sorting 04) and the compressed-row layout of a graph (Graphs 01) both begin by building one.
- **Histograms and percentiles.** A cumulative histogram is a prefix sum of the bucket counts; the 99th percentile is the first bucket whose cumulative count reaches 99% of the total, the same search as in section 6.
- **Parallel scan.** The loop `P[i] = P[i − 1] + a[i − 1]` is a chain of dependent additions, so it looks serial. But `+` is associative, so a tree of partial sums finishes in about log₂ n steps. This is the *parallel prefix* computation of a carry-lookahead adder (tutorial CO 05, section 2.1) and a standard GPU primitive, called a **scan** (CO 22, section 6.3 does the related reduction by hand). C++17 names the library versions `std::inclusive_scan` and `std::exclusive_scan`; our `P` is the *exclusive* scan with the total added at the end.

---

## 8. Choosing the tool

| Situation | Use |
|---|---|
| Fixed array, many range-sum queries | prefix sums |
| Fixed grid, many rectangle queries | 2D prefix sums |
| Count or find subarrays with a given sum, negatives allowed | prefix sums and a hash map |
| Same, but all numbers are non-negative and O(1) space matters | sliding window (tutorial 03) |
| Many range updates, then one read | difference array |
| Updates and queries mixed | Fenwick tree or segment tree (Trees 20 to 22) |
| Range minimum or maximum on a fixed array | sparse table (Trees 19) |
| Maximum of every window of size k | monotonic deque (Stacks and Queues 03) |
| Shortest subarray with sum at least K, negatives allowed | prefix sums with a monotonic deque (Stacks and Queues 03) |

---

## 9. Mistakes that cost points

1. **No leading zero in `P`, or `P[r] − P[l]` instead of `P[r + 1] − P[l]`.** Use `n + 1` entries and one formula.
2. **`int` for sums.** Use `long long`.
3. **Forgetting to seed the map with `{0: 1}`** (or `first[0] = −1`). Every subarray that starts at index 0 is lost.
4. **Inserting the current prefix before looking up.** Wrong whenever `K = 0`.
5. **Using a sliding window when numbers can be negative** (tutorial 03, section 4).
6. **Negative remainders in C++.** `-1 % 5 == -1`; normalise with `((x % k) + k) % k`.
7. **Overwriting the first index when you want the longest subarray** (or keeping the first when you want the shortest).
8. **Difference array: no spare slot for `r + 1`, or reading the array before taking the prefix sum.** The array holds differences until you sum it.
9. **2D: forgetting the overlap term.** The build and the query each need the "+ P[r1][c1]" or "− P[i][j]" correction; draw the four regions if unsure.
10. **Mixing inclusive and exclusive coordinates.** Decide once: `l` and `r` are both included, `P[i]` is exclusive of `a[i]`.

---

## 10. Questions you will be asked

**How do you answer many range-sum queries on a fixed array?** Build prefix sums in O(n), then each query is `P[r + 1] − P[l]` in O(1).

**Why does "subarray sum equals K" need prefix sums and not a sliding window?** With negative numbers, shrinking the window can increase the sum, so the window has no monotonic rule to follow. Prefix sums turn it into "find an earlier prefix equal to the current one minus K", which does not depend on signs.

**Why do you put `{0: 1}` in the map first?** It stands for the empty prefix, so that subarrays beginning at index 0 are counted.

**What is the time and space?** O(n) expected time and O(n) space for the hash map. Sorting the prefixes instead gives O(n log n) with a guaranteed bound.

**The array also changes between queries. Now what?** A Fenwick tree or segment tree: O(log n) per update and per query.

**Can you use prefix sums for range maximum?** No. Maximum has no inverse, so the prefix before `l` cannot be taken out. Use a sparse table (static) or a segment tree (with updates).

**A million range updates, then print the array?** A difference array: O(1) per update and one O(n) pass at the end.

**How do prefix sums relate to monitoring counters?** A cumulative counter is a prefix sum of the events; the rate over a window is a difference of two readings (a range sum), and unsigned subtraction survives one wrap-around.

**The input is a stream and you want the number of subarrays with sum K so far?** The map approach is already online: one pass, O(1) per element, memory proportional to the number of distinct prefix values. If the numbers are non-negative you can bound memory with a window instead.

---

## 11. Problems

**How to use this section.** Try each problem for 15 minutes (easy) or 30 minutes (medium) before reading anything. If you are stuck, read only the **How** lines, then try again, and only then open the answer at the bottom. Log each attempt in the [practice log](../practice_log.md). Ask first: is there an inverse operation? Is it a query on a fixed array, a count of subarrays, or a batch of updates?

### On paper

**Problem A: Trace it.** For `a = {2, −1, 3, 4, −2, 5}`: (a) write `P`; (b) compute the sum of `a[1..4]` from `P`; (c) run `countSubarraysWithSum` with `K = 5` by hand, in a table like the one in section 2, and list the subarrays it finds.
*Where:* paper. *How:* `P` starts with 0; for (c) write the prefix, the value you look up (prefix − 5), and how many earlier prefixes match.

**Problem B: Pick the tool.** For each, say what you would use.
(1) A fixed array of 10⁶ numbers and 10⁵ queries "sum of `a[l..r]`".
(2) The number of subarrays with sum 0 in an array that may contain negative numbers.
(3) 10⁵ updates "add `v` to `a[l..r]`", then print the final array.
(4) The same updates, but after each one you must answer "what is `a[i]` now?".
(5) The minimum of `a[l..r]` for many queries on a fixed array.
(6) The longest subarray with sum at most 100, in an array of positive numbers.
*Where:* paper. *How:* use the table in section 8.

**Problem C: Build the 2D difference grid.** Start with a 3 × 3 grid of zeros. Add 1 to the rectangle with corners (0,0) and (1,1), then add 2 to the rectangle with corners (1,1) and (2,2). Write the difference grid (4 × 4) after both updates, then recover the 3 × 3 result by 2D prefix sums.
*Where:* paper. *How:* use the four-corner rule of section 5.2 for each update and add the two grids.

### In code

**Problem 1: Range Sum Query - Immutable** (LeetCode 303, easy)
*Where:* leetcode.com/problems/range-sum-query-immutable. *Goal:* a class that is built from an array and answers `sumRange(left, right)` many times.
*How:* build `P` in the constructor, answer with one subtraction.

**Problem 2: Range Sum Query 2D - Immutable** (LeetCode 304, medium)
*Where:* leetcode.com/problems/range-sum-query-2d-immutable. *Goal:* the same for a grid and rectangles.
*How:* the 2D table of section 4. Write the four regions on paper before you code the formula.

**Problem 3: Subarray Sum Equals K** (LeetCode 560, medium)
*Where:* leetcode.com/problems/subarray-sum-equals-k. *Goal:* the number of subarrays whose sum is `k` (numbers may be negative).
*How:* section 2. Seed the map, look up `prefix − k`, then insert.

**Problem 4: Contiguous Array** (LeetCode 525, medium)
*Where:* leetcode.com/problems/contiguous-array. *Goal:* the length of the longest subarray of a 0/1 array with equally many 0s and 1s.
*How:* treat 0 as −1, so the target is a sum of 0. Store the first index of each prefix value.

**Problem 5: Subarray Sums Divisible by K** (LeetCode 974, medium)
*Where:* leetcode.com/problems/subarray-sums-divisible-by-k. *Goal:* the number of subarrays whose sum is divisible by `k`.
*How:* equal prefix remainders. Normalise negative remainders.

**Problem 6: Corporate Flight Bookings** (LeetCode 1109, medium)
*Where:* leetcode.com/problems/corporate-flight-bookings. *Goal:* given bookings `[first, last, seats]` on flights `1..n`, return the seats booked on each flight.
*How:* a difference array of size `n + 2` (flights are numbered from 1), then one prefix pass.

**More practice, once these feel easy:** Car Pooling (1094, section 5.1), Random Pick with Weight (528, section 6), Number of Submatrices That Sum to Target (1074, hard, section 4), Continuous Subarray Sum (523, divisibility with a length condition). On CSES: Subarray Sums II, Subarray Divisibility and Forest Queries (cses.fi/problemset). The [USACO Guide module on prefix sums](https://usaco.guide/silver/prefix-sums) has a graded list.

---

## Answers

Try before reading. Code is written as plain functions or classes; LeetCode wraps functions in `class Solution { public: ... };`.

### Problem A

(a) `P = {0, 2, 1, 4, 8, 6, 11}`.
(b) `a[1..4] = −1 + 3 + 4 − 2 = 4`, and `P[5] − P[1] = 6 − 2 = 4`.
(c) With `K = 5` (map starts as `{0: 1}`):

| Element | Prefix | Need (prefix − 5) | Matches | Total |
|---|---|---|---|---|
| 2 | 2 | −3 | 0 | 0 |
| −1 | 1 | −4 | 0 | 0 |
| 3 | 4 | −1 | 0 | 0 |
| 4 | 8 | 3 | 0 | 0 |
| −2 | 6 | 1 | 1 (prefix 1, after the element −1) | 1 |
| 5 | 11 | 6 | 1 (prefix 6) | 2 |

Two subarrays: `3, 4, −2` (sum 5) and `5` (sum 5).

### Problem B

1. **Prefix sums.** O(n) to build, O(1) per query.
2. **Prefix sums and a hash map** (K = 0): count pairs of equal prefix values. A window fails with negative numbers.
3. **Difference array.** O(1) per update and one pass at the end.
4. **Fenwick tree or lazy segment tree** (Trees 20 and 22): updates and reads are mixed, so a difference array is not enough.
5. **Sparse table.** Minimum has no inverse, so prefix sums cannot do it.
6. **Sliding window** (tutorial 03, the "longest" template): all numbers are positive.

### Problem C

The first update puts its four marks at (0,0) with +1, (0,2) with −1, (2,0) with −1 and (2,2) with +1. The second puts its marks at (1,1) with +2, (1,3) with −2, (3,1) with −2 and (3,3) with +2. No two marks share a cell here, so the difference grid is just the eight marks:

```text
   D:   +1   0  -1   0          result by 2D prefix sums:
         0  +2   0  -2             1  1  0
        -1   0  +1   0             1  3  2
         0  -2   0  +2             0  2  2
```

Check the result against the direct reading: the 2 × 2 block at the top left got +1, the 2 × 2 block at the bottom right got +2, and the cell (1,1), which is in both, holds 1 + 2 = 3.

### Problem 1: Range Sum Query - Immutable

```cpp
// LeetCode 303.
class NumArray {
    std::vector<long long> p_;
public:
    explicit NumArray(const std::vector<int>& nums) : p_(buildPrefix(nums)) {}
    int sumRange(int left, int right) const { return (int)(p_[right + 1] - p_[left]); }
};
```

### Problem 2: Range Sum Query 2D - Immutable

```cpp
// LeetCode 304.
class NumMatrix {
    Prefix2D t_;
public:
    explicit NumMatrix(const std::vector<std::vector<int>>& m) : t_(m) {}
    int sumRegion(int row1, int col1, int row2, int col2) const {
        return (int)t_.sum(row1, col1, row2, col2);
    }
};
```

It wraps the `Prefix2D` of section 4. O(m × n) to build, O(1) per query.

### Problem 3: Subarray Sum Equals K

```cpp
int subarraySum(const std::vector<int>& nums, int k) {
    return (int)countSubarraysWithSum(nums, k);
}
```

It calls the function of section 2 (the LeetCode signature has `int k`). O(n) time and space. For `{1, 1, 1}` and `k = 2` the answer is 2; for `{1, 2, 3}` and `k = 3` it is 2 (`1, 2` and `3`).

### Problem 4: Contiguous Array

The function `findMaxLength` of section 3.1 is the answer: balance = ones − zeros, store the first index of each balance (starting with 0 at index −1), and on a repeat take the distance. O(n) time and space. For `{0, 1, 0}` the answer is 2.

### Problem 5: Subarray Sums Divisible by K

The function `subarraysDivByK` of section 3.2 is the answer. O(n) time, O(k) space. The result can be as large as n(n + 1)/2, which fits in an `int` for LeetCode's limits; use `long long` if n can be larger.

### Problem 6: Corporate Flight Bookings

```cpp
// LeetCode 1109: seats booked on flights 1..n; each booking is {first, last, seats}.
std::vector<int> corpFlightBookings(const std::vector<std::vector<int>>& bookings, int n) {
    std::vector<int> diff(n + 2, 0);                 // flights are 1-indexed; slot n + 1 absorbs the "stop" marker
    for (const auto& b : bookings) {
        diff[b[0]] += b[2];
        diff[b[1] + 1] -= b[2];
    }
    std::vector<int> answer(n);
    int running = 0;
    for (int f = 1; f <= n; ++f) {
        running += diff[f];
        answer[f - 1] = running;
    }
    return answer;
}
```

O(n + number of bookings). For bookings `{1,2,10}`, `{2,3,20}`, `{2,5,25}` and `n = 5` the answer is `{10, 55, 45, 25, 25}`.

---

## Cheat sheet

- **Prefix sums:** `P[0] = 0`, `P[i + 1] = P[i] + a[i]`; `sum(l..r) = P[r + 1] − P[l]`. O(n) build, O(1) query. Use `long long`.
- Needs an **inverse** operation: works for sum, count, XOR; **not** for min, max, GCD.
- **Subarray sum = K** (negatives allowed): for each prefix, count earlier prefixes equal to `prefix − K`. Seed the map with `{0: 1}`; look up **before** inserting. O(n).
- Longest: store the **first** index. Shortest: store the **latest** index. Exists: a set.
- **0/1 balance:** map 0 to −1, then sum 0. **Divisible by K:** equal prefix remainders; normalise with `((x % k) + k) % k`.
- **2D:** `P[i + 1][j + 1] = a + up + left − corner`; query = big block − above − left + corner.
- **Difference array:** `D[l] += v`, `D[r + 1] −= v`, then a prefix sum. O(1) per update; read only at the end. Inverse of prefix sums (`adjacent_difference` / `partial_sum`).
- **Weighted pick:** prefix sums of weights plus `lower_bound` on a random target.
- Counters, counting sort offsets, histogram percentiles and parallel scan are all prefix sums.
- Mixed updates and queries: Fenwick or segment tree. Static range min/max: sparse table.

---

## Sources

- Counts, outputs and timings in this tutorial were produced by running the code shown in it on the machine described in the text. Times are that machine's, not yours.
- The algorithms and data structures are standard; for further reading see the "Extra practice and resources" section of the [area plan](00_plan.md), which lists the textbook chapters and free references for this area. The explanations and code are my own.
- LeetCode numbers and titles are from memory; check a number before you rely on it.

**Next:** tutorial 05, Subarray Patterns and Kadane. Say "next" when you are ready.
