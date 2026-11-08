# DSA 01.05: Subarray Patterns and Kadane

*Part of the [Arrays and Strings plan](00_plan.md) · Time: about 4 hours (1.5 reading, 2.5 problems) · You need: tutorials [01](01_complexity_and_arrays_in_memory.md) and [04](04_prefix_sums_and_difference_arrays.md) · Companion code: [`05_subarray_patterns_demo.cpp`](05_subarray_patterns_demo.cpp)*

By the end of this tutorial you can:

- derive Kadane's algorithm from a one-line recurrence, prove it, and adapt it to return the subarray, to allow the empty subarray, and to work on a circular array;
- solve maximum product subarray, where one number is not enough state;
- find the majority element with Boyer-Moore in O(1) space, say exactly why it works, and extend it to "more than n/k times" (Misra-Gries);
- solve the whole stock-trading family (one transaction, unlimited, fee, cooldown, at most k) from one state-machine idea;
- recognise when the "best run ending here" idea applies, and when it does not.

Every function in this tutorial is in the companion file, which compiles with `-Wall -Wextra -fsanitize=address,undefined` and checks each function against a brute force on random inputs.

---

## 1. The idea: the best subarray that ends here

Tutorial 04 counted subarrays with a given sum. This tutorial asks a different question: **which subarray is the best**, for some definition of best (largest sum, largest product, and so on).

There are O(n²) subarrays. The trick is to group them by where they **end**. If for every position `i` you know the best subarray that ends exactly at `i`, the overall best is the largest of those n answers. And the best subarray ending at `i` is built from the best subarray ending at `i − 1`:

> a subarray ending at `i` is either the single element `a[i]`, or a subarray ending at `i − 1` with `a[i]` added at the end.

So you only need to carry **one number** from each position to the next. That is dynamic programming with the state squeezed into a single variable. The same shape solves every problem in this tutorial, with a different number (or two) carried along.

---

## 2. Kadane's algorithm: maximum subarray sum

*Question:* given an array of integers (possibly negative), find the largest sum of a **non-empty** contiguous subarray.

Let `cur(i)` be the best sum of a subarray that ends at `i`. By the observation above:

> **cur(i) = max( a[i],  cur(i − 1) + a[i] )**

and the answer is the largest `cur(i)`. Read it in words: if the best run ending at `i − 1` has a positive sum, extend it; if it has a negative (or zero) sum, it can only hurt, so start fresh at `i`.

```cpp
// Best sum of a non-empty subarray. Requires a.size() >= 1.
long long maxSubarraySum(const std::vector<int>& a) {
    long long cur = a[0];                              // best sum of a subarray that ends at the current element
    long long best = a[0];                             // best over all ends seen so far
    for (size_t i = 1; i < a.size(); ++i) {
        cur = std::max<long long>(a[i], cur + a[i]);   // extend the run, or start a new one here
        best = std::max(best, cur);
    }
    return best;
}
```

Trace on `{−2, 1, −3, 4, −1, 2, 1, −5, 4}`:

| i | a[i] | cur = max(a[i], cur + a[i]) | best |
|---|---|---|---|
| 0 | −2 | −2 | −2 |
| 1 | 1 | max(1, −2 + 1) = **1** (start fresh) | 1 |
| 2 | −3 | max(−3, 1 − 3) = −2 | 1 |
| 3 | 4 | max(4, −2 + 4) = **4** (start fresh) | 4 |
| 4 | −1 | 3 | 4 |
| 5 | 2 | 5 | 5 |
| 6 | 1 | 6 | **6** |
| 7 | −5 | 1 | 6 |
| 8 | 4 | 5 | 6 |

The answer is 6, the subarray `4, −1, 2, 1`. Cost: **O(n) time, O(1) space**, one pass.

### 2.1 Why it is correct

There is nothing clever to prove. Every subarray ending at `i` is either `[i]` alone or (a subarray ending at `i − 1`) + `a[i]`. The best subarray of the second kind is the best one ending at `i − 1`, plus `a[i]`, because adding the same number to every candidate does not change which candidate is largest. So the recurrence for `cur(i)` is exact, by induction from `cur(0) = a[0]`, and the answer is the maximum over all possible ends.

A second way to say the same thing: **a best subarray never has a prefix with a negative sum**, because cutting that prefix off would give a larger sum. Kadane's algorithm throws the running sum away exactly when it turns negative.

### 2.2 Details that decide the answer

1. **Initialise with `a[0]`, not 0.** With `best = 0` an array of all negative numbers returns 0, which is the sum of the *empty* subarray, not of any element. Ask whether the empty subarray is allowed. If it is, the code is shorter:

```cpp
// Best sum of a possibly empty subarray: 0 if every element is negative.
long long maxSubarraySumOrEmpty(const std::vector<int>& a) {
    long long cur = 0, best = 0;
    for (int x : a) {
        cur = std::max<long long>(0, cur + x);         // a negative running sum is dropped
        best = std::max(best, cur);
    }
    return best;
}
```

2. **Return the subarray, not only the sum.** Remember where the current run started, and when `cur` beats the best, record the start and the current index.

```cpp
struct Best {
    long long sum;
    int l, r;                                          // the subarray a[l..r], both ends included
};

Best maxSubarrayRange(const std::vector<int>& a) {
    Best best{a[0], 0, 0};
    long long cur = a[0];
    int start = 0;                                     // where the current run began
    for (int i = 1; i < (int)a.size(); ++i) {
        if (cur < 0) { cur = a[i]; start = i; }        // the run so far only hurts: start again at i
        else cur += a[i];
        if (cur > best.sum) best = {cur, start, i};
    }
    return best;
}
```

   On the array above it returns the sum 6 with `l = 3` and `r = 6`. When several subarrays tie, it returns one of them; say which one if the interviewer asks.

3. **Use `long long`** for the sums if the elements or the length are large.

### 2.3 The same thing from prefix sums

With the prefix array `P` of tutorial 04, the sum of `a[l..r]` is `P[r + 1] − P[l]`. For a fixed end `r`, the best start is the **smallest earlier prefix**:

```cpp
// The same answer from prefix sums (tutorial 04): best P[r + 1] - P[l] with l <= r.
long long maxSubarrayByPrefix(const std::vector<int>& a) {
    long long prefix = 0, minBefore = 0, best = LLONG_MIN;   // minBefore = smallest P[l] allowed for the current end
    for (int x : a) {
        prefix += x;                                   // this is P[r + 1]
        best = std::max(best, prefix - minBefore);
        minBefore = std::min(minBefore, prefix);       // P[r + 1] may now start a later subarray
    }
    return best;
}
```

Look at what this is: take the prefix sums as a price chart; the best subarray is **buy at the lowest earlier price, sell at today's price**. That is the stock problem of tutorial 01, and section 7.1 below shows the two are the same problem. Both views are worth knowing: Kadane is easier to code, the prefix view explains variants such as "length at least L" (section 3.3).

### 2.4 Divide and conquer: the form a segment tree uses

Split the array in two halves. The best subarray lies entirely in the left half, entirely in the right half, or **crosses the middle**. A crossing subarray is the best *suffix* of the left half plus the best *prefix* of the right half. So for each range keep four numbers: the total, the best prefix, the best suffix and the best subarray, and merge two ranges with a few additions:

```cpp
struct Node {
    long long sum, prefix, suffix, best;               // best prefix, best suffix and best subarray are all non-empty
};

Node leaf(int x) { return {x, x, x, x}; }

Node combine(const Node& L, const Node& R) {
    return {L.sum + R.sum,
            std::max(L.prefix, L.sum + R.prefix),
            std::max(R.suffix, R.sum + L.suffix),
            std::max({L.best, R.best, L.suffix + R.prefix})};
}

Node maxSubarrayDC(const std::vector<int>& a, int lo, int hi) {   // the half-open range [lo, hi), non-empty
    if (hi - lo == 1) return leaf(a[lo]);
    int mid = lo + (hi - lo) / 2;
    return combine(maxSubarrayDC(a, lo, mid), maxSubarrayDC(a, mid, hi));
}
```

The merge is O(1), so the recurrence is `T(n) = 2T(n/2) + O(1) = O(n)`: no slower than Kadane, with O(log n) stack. Two reasons this matters more than it looks:

- A **segment tree** stores one `Node` per tree node. Then "maximum subarray inside `a[l..r]`" with point updates costs O(log n) per operation (Trees 21). The interview follow-up "what if the array changes?" is answered by this struct.
- `combine` is **associative**, so the array can be cut into blocks, each block summarised on a different thread (or GPU block), and the summaries combined in a tree. The same pattern as a parallel reduction or scan (CO 22, section 6.3).

---

## 3. Kadane with more state, and in more dimensions

### 3.1 One deletion allowed (LeetCode 1186)

*Question:* the largest sum of a non-empty subarray after you may delete **at most one element** from it.

Carry two numbers instead of one: the best sum ending here with nothing deleted, and the best sum ending here with one element already deleted.

```cpp
// LeetCode 1186: best sum of a non-empty subarray after deleting at most one element of it.
long long maxSumOneDeletion(const std::vector<int>& a) {
    const long long NEG = LLONG_MIN / 4;               // "impossible", but safe to add to
    long long noDel = a[0], oneDel = NEG, best = a[0]; // best sums of a subarray ending here
    for (size_t i = 1; i < a.size(); ++i) {
        oneDel = std::max(noDel, oneDel + a[i]);       // delete a[i] itself, or extend a run that already lost one
        noDel = std::max<long long>(a[i], noDel + a[i]);
        best = std::max({best, noDel, oneDel});
    }
    return best;
}
```

`oneDel` is either "delete `a[i]` itself" (so the run is whatever ended at `i − 1` with nothing deleted) or "an earlier element is already gone, extend". Trace on `{1, −2, 0, 3}`:

| a[i] | noDel | oneDel | best |
|---|---|---|---|
| 1 | 1 | impossible | 1 |
| −2 | −1 | **1** (delete the −2) | 1 |
| 0 | 0 | 1 | 1 |
| 3 | 3 | **4** (1, 0, 3 with −2 deleted) | **4** |

This is the first appearance of a pattern that returns in section 7: **a small number of named states, and each step computes the new value of every state from the old values.** Update `oneDel` before `noDel`, because it reads the old `noDel`.

### 3.2 Largest sum rectangle in a grid

For a grid, fix the **top and bottom rows** (O(m²) pairs), add the rows between them column by column into one array `col`, and run Kadane along `col`: the best subarray of `col` is the best rectangle between those two rows. As the bottom row moves down, `col` is updated by one row, so the work per pair is O(n).

```cpp
// Largest sum of a rectangle of cells, with O(rows^2 * cols) work. Requires a non-empty grid.
long long maxRectangleSum(const std::vector<std::vector<int>>& g) {
    int m = (int)g.size(), n = (int)g[0].size();
    long long best = LLONG_MIN;
    for (int top = 0; top < m; ++top) {
        std::vector<long long> col(n, 0);              // col[j] = g[top][j] + ... + g[bottom][j]
        for (int bottom = top; bottom < m; ++bottom) {
            long long cur = 0;
            for (int j = 0; j < n; ++j) {
                col[j] += g[bottom][j];
                cur = std::max(col[j], cur + col[j]);  // Kadane along the columns
                best = std::max(best, cur);
            }
        }
    }
    return best;
}
```

Total **O(m² × n)**, against O(m² × n²) if you re-add every rectangle. If the grid is tall and narrow (m > n), swap the roles of rows and columns first so the squared factor is the smaller side. This is the 2D version of the row-pair trick from tutorial 04, section 4. On the grid `{{1, −2, 3}, {−4, 5, −1}, {2, −3, 6}}` the best rectangle sums to 8, for example the whole right-hand column (3 − 1 + 6).

### 3.3 Variations and what to reach for

| Variation | Idea |
|---|---|
| Smallest sum (minimum subarray) | Kadane with `min`, or negate the array |
| Largest absolute sum (LeetCode 1749) | `max(maxSubarray, −minSubarray)` |
| Circular array | section 5 |
| Product instead of sum | section 4 |
| Length **at least** L | prefix sums: for each `i`, `P[i] −` the minimum of `P[0..i − L]`, kept as a running minimum (`i` counts elements taken, as in tutorial 04) |
| Length **between** L and U | the same, with the minimum taken over a sliding window of `P`: a monotonic deque (Stacks and Queues 03; CSES "Maximum Subarray Sum II") |
| Sum at most K (LeetCode 363, grid) | row pairs as in 3.2, then prefix sums with an ordered set instead of Kadane |
| Exactly k disjoint subarrays | a DP with `k` states (DP area) |
| Array changes between queries | segment tree with the `Node` of section 2.4 |

---

## 4. Maximum product subarray (LeetCode 152)

*Question:* the largest **product** of a non-empty contiguous subarray.

The recurrence `cur(i) = max(a[i], cur(i − 1) × a[i])` is no longer enough, because a negative number turns the **smallest** product into the **largest** one. On `{−2, 3, −4}` the best run ending at the middle is 3, but the best ending at the last element is `(−2) × 3 × (−4) = 24`, and it comes from the *most negative* product so far. So carry both the largest and the smallest product of a subarray ending here, and swap them when `a[i]` is negative:

```cpp
long long maxProductSubarray(const std::vector<int>& a) {
    long long hi = a[0], lo = a[0], best = a[0];       // largest and smallest product of a subarray ending here
    for (size_t i = 1; i < a.size(); ++i) {
        long long x = a[i];
        if (x < 0) std::swap(hi, lo);                  // a negative factor turns the largest product into the smallest
        hi = std::max(x, hi * x);
        lo = std::min(x, lo * x);
        best = std::max(best, hi);
    }
    return best;
}
```

Trace on `{2, 3, −2, 4}`:

| a[i] | hi (largest) | lo (smallest) | best |
|---|---|---|---|
| 2 | 2 | 2 | 2 |
| 3 | 6 | 3 | 6 |
| −2 | −2 (swap, then max(−2, 3 × −2)) | −12 | 6 |
| 4 | 4 | −48 | 6 |

The answer is 6 (`2, 3`). On `{−2, 3, −4}` the last row is `hi = 24`, so the answer is 24. **Zeros** need no special case: `max(x, hi × x)` with `x = 0` gives 0 for both numbers, which restarts the run at the next element. On `{−2, 0, −1}` the answer is 0.

**A second method: scan from both ends.** Inside a stretch with no zeros, the product is positive if the number of negatives is even (take everything), and otherwise the best choice drops everything up to and including the first negative, or everything from the last negative on. So the best product is always a **prefix or a suffix** of a zero-free stretch (or 0 itself):

```cpp
// The same answer from the two ends: the best product is a prefix or a suffix of a zero-free stretch.
long long maxProductScan(const std::vector<int>& a) {
    int n = (int)a.size();
    long long best = a[0], front = 1, back = 1;
    for (int i = 0; i < n; ++i) {
        front = (front == 0 ? 1 : front) * a[i];       // product from the last zero (or the start) up to i
        back = (back == 0 ? 1 : back) * a[n - 1 - i];  // product from the end down to n - 1 - i
        best = std::max({best, front, back});
    }
    return best;
}
```

`front` is the product from the last zero (or the start) up to `i`, `back` the same from the other end; a zero resets the running product to 1 for the next element. Both methods are O(n) time and O(1) space. Mention that products grow fast: LeetCode promises the answer fits in 32 bits; in general you would need big integers or logarithms.

---

## 5. Maximum sum in a circular array (LeetCode 918)

*Question:* the array is a circle (the last element is next to the first). Find the best non-empty subarray, which may wrap around the end.

There are two cases:

1. The best subarray does **not** wrap: ordinary Kadane.
2. It **wraps**: it takes a suffix and a prefix. What it leaves out is a block in the *middle* of the array, and to make the sum largest that left-out block must have the **smallest** sum. So the wrapping answer is `total − (minimum subarray sum)`.

Both come out of one pass: run Kadane for the maximum and the same recurrence with `min` for the minimum. The answer is the larger of the two cases, with one trap: if **every element is negative**, the minimum subarray is the whole array and `total − min` is 0, the empty subarray, which is not allowed. In that case the answer is simply the ordinary Kadane result.

```cpp
// LeetCode 918: best sum of a non-empty subarray of the array viewed as a circle.
long long maxCircularSubarray(const std::vector<int>& a) {
    long long total = 0, curMax = 0, curMin = 0, bestMax = a[0], bestMin = a[0];
    for (int x : a) {
        curMax = std::max<long long>(x, curMax + x);
        bestMax = std::max(bestMax, curMax);
        curMin = std::min<long long>(x, curMin + x);
        bestMin = std::min(bestMin, curMin);
        total += x;
    }
    if (bestMax < 0) return bestMax;                   // every element is negative: total - bestMin would be the empty subarray
    return std::max(bestMax, total - bestMin);         // a middle block, or everything except the worst middle block
}
```

Trace on `{5, −3, 5}`: total 7, the best non-wrapping run is the whole array (7), the minimum subarray is `−3`, so the wrapping value is `7 − (−3) = 10`: the two fives next to each other around the circle. The answer is 10. On `{−3, −2, −3}` the maximum is −2, all elements are negative, and the answer is −2. O(n) time, O(1) space.

---

## 6. Majority element and heavy hitters

### 6.1 Boyer-Moore voting (LeetCode 169)

*Question:* find the element that occurs **more than n/2 times**, in O(n) time and O(1) space.

Think of a vote. Keep a `candidate` and a `count`. For each element: if `count` is 0, nominate it; then add 1 if it equals the candidate, subtract 1 if not. Each "disagreeing" element cancels one vote.

```cpp
// The only value that can be a majority element. Meaningless if there is none.
int majorityCandidate(const std::vector<int>& a) {
    int candidate = 0, count = 0;
    for (int x : a) {
        if (count == 0) candidate = x;                 // the votes so far cancelled out: nominate x
        count += (x == candidate) ? 1 : -1;
    }
    return candidate;
}
```

Trace on `{2, 2, 1, 1, 1, 2, 2}` (the candidate changes along the way and still ends correctly):

| x | candidate | count |
|---|---|---|
| 2 | 2 | 1 |
| 2 | 2 | 2 |
| 1 | 2 | 1 |
| 1 | 2 | 0 |
| 1 | 1 (count was 0, nominate) | 1 |
| 2 | 1 | 0 |
| 2 | 2 (nominate) | 1 |

**Why it works.** At every moment the elements seen so far split into pairs of *different* values that cancelled each other, plus `count` leftover copies of the `candidate`. A value other than the candidate can appear at most once per pair, so at most `(n − count) / 2 ≤ n/2` times: it cannot be a strict majority. So **if a majority element exists, it is the candidate**.

The "if" matters. On `{1, 2, 3}` there is no majority, and the code still returns 3. When the problem does not promise a majority, add a second pass that counts the candidate:

```cpp
// The value that occurs more than n / 2 times, if there is one.
std::optional<int> majorityElement(const std::vector<int>& a) {
    if (a.empty()) return std::nullopt;
    int c = majorityCandidate(a);
    long long occurrences = std::count(a.begin(), a.end(), c);   // the second pass: confirm it
    if (occurrences * 2 > (long long)a.size()) return c;
    return std::nullopt;
}
```

| Method | Time | Extra space | Note |
|---|---|---|---|
| Hash map of counts | O(n) | O(n) | simplest; also finds the answer to "n/k" |
| Sort, take the middle | O(n log n) | O(1) or O(n) | the middle element of a sorted array is the majority if it exists |
| Count each bit | O(32 n) | O(1) | bit `b` of the majority is the majority bit among all elements |
| Random pick and check | O(n) expected | O(1) | the majority is hit with probability above 1/2 each try |
| **Boyer-Moore** | **O(n)** | **O(1)** | one pass; works on a stream; needs the verification pass if no majority is promised |

### 6.2 More than n/k times: Misra-Gries

*Question:* find all elements that occur **more than n/3 times** (LeetCode 229). Or, in general, more than n/k times.

There can be fewer than k such elements (two for n/3, since three would need more than n elements), so keep **k − 1 counters** instead of one. When a value that is not being tracked arrives and all k − 1 counters are in use, there are now k different values in play: cancel one of each, which means decreasing every counter by 1 (and dropping the counters that reach 0) and ignoring the new value.

```cpp
// Misra-Gries: at most k - 1 counters. Every value occurring more than n / k times is left in the map.
std::unordered_map<int, int> misraGries(const std::vector<int>& a, int k) {
    std::unordered_map<int, int> counters;
    for (int x : a) {
        auto it = counters.find(x);
        if (it != counters.end()) {
            ++it->second;
        } else if ((int)counters.size() < k - 1) {
            counters[x] = 1;
        } else {                                       // k different values now: cancel one of each
            for (auto i = counters.begin(); i != counters.end();) {
                if (--i->second == 0) i = counters.erase(i);
                else ++i;
            }
        }
    }
    return counters;
}
```

Trace with `k = 3` (two counters) on `{1, 2, 1, 3, 1, 2, 1, 4, 1}`, showing the counters after each element:

| x | counters |
|---|---|
| 1 | {1: 1} |
| 2 | {1: 1, 2: 1} |
| 1 | {1: 2, 2: 1} |
| 3 | {1: 1} (3 is new and the table is full: decrement all, 2 drops out) |
| 1 | {1: 2} |
| 2 | {1: 2, 2: 1} |
| 1 | {1: 3, 2: 1} |
| 4 | {1: 2} (decrement all) |
| 1 | {1: 3} |

**The guarantee.** Each decrement step removes k items from play at once (k − 1 counted ones and the new one), so there can be at most n/k such steps. So every counter is too low by at most n/k, and a value that occurs more than n/k times keeps a positive counter: **no true heavy hitter is ever lost**. The converse is false: a counter may sit on a value that is not frequent enough (the 2 holds one for a while above, though it occurs only twice). Counts are under-estimates, so as in 6.1 a **second pass** confirms the candidates:

```cpp
// All values that occur more than n / k times (LeetCode 229 is k = 3), in increasing order.
std::vector<int> heavyHitters(const std::vector<int>& a, int k) {
    std::vector<int> result;
    for (const auto& [value, estimate] : misraGries(a, k)) {
        long long occurrences = std::count(a.begin(), a.end(), value);   // confirm with a second pass
        if (occurrences * k > (long long)a.size()) result.push_back(value);
    }
    std::sort(result.begin(), result.end());
    return result;
}
```

For LeetCode 229 call `heavyHitters(nums, 3)`. With `k = 2` this is Boyer-Moore with one counter. **Cost:** the decrement step touches k − 1 counters, but each decrement undoes an earlier increment, so the total decrement work is at most the number of increments: O(1) amortised per element, O(n) overall with O(k) space. The weak point is the hash map; for small `k` a short array of pairs is faster.

---

## 7. The stock-trading family

Six LeetCode problems share one picture: you see prices day by day, you may hold at most one share, and you want the most profit.

| Problem | Rule | Method |
|---|---|---|
| 121 | one transaction | Kadane on the daily changes, or "lowest price so far" |
| 122 | any number | add every rise |
| 714 | any number, a fee per transaction | two states |
| 309 | any number, one rest day after selling | three states |
| 123 | at most 2 transactions | `buy[j]` / `sell[j]` states |
| 188 | at most k transactions | the same, with `j` up to `k` |

### 7.1 One transaction (121): Kadane on the changes

Buying on day `l` and selling on day `r` earns `p[r] − p[l]`, which equals the sum of the **daily changes** over days `l + 1` to `r`. So the best single transaction is the maximum subarray of the array of daily changes, with the empty choice (do nothing, profit 0) allowed:

```cpp
// One transaction: Kadane on the daily changes, with the empty answer (0) allowed.
long long maxProfitOne(const std::vector<int>& prices) {
    long long cur = 0, best = 0;
    for (size_t i = 1; i < prices.size(); ++i) {
        cur = std::max<long long>(0, cur + prices[i] - prices[i - 1]);   // the best gain of a run that ends today
        best = std::max(best, cur);
    }
    return best;
}
```

This is section 2.2's empty-allowed Kadane applied to `p[i] − p[i − 1]`, and it is also the "lowest price so far" loop of tutorial 01 in different clothes. On `{7, 1, 5, 3, 6, 4}` the changes are `−6, +4, −2, +3, −2`, and the best run is `+4, −2, +3 = 5`.

### 7.2 Any number of transactions (122): add every rise

A profit made by holding over several days is the sum of the single-day rises in that period, so the best you can do is take **every** day on which the price goes up and ignore every fall:

```cpp
// Any number of transactions (LeetCode 122): collect every daily rise.
long long maxProfitUnlimited(const std::vector<int>& prices) {
    long long profit = 0;
    for (size_t i = 1; i < prices.size(); ++i)
        if (prices[i] > prices[i - 1]) profit += prices[i] - prices[i - 1];
    return profit;
}
```

On `{7, 1, 5, 3, 6, 4}`: rises of 4 (1 to 5) and 3 (3 to 6) give 7. You never need two shares at once: buy at the start of each rising stretch and sell at its end.

### 7.3 The state-machine view: fee and cooldown

For anything more complex, stop thinking about "which days" and draw the **states** you can be in at the end of a day, and the **moves** between them:

```text
     buy (pay the price)
 CASH ------------------->  HOLD
 (no share)  <-------------  (one share)
     sell (get the price, pay the fee)
```

Keep the **best balance for each state**. Every day, the new value of each state is the best of "stay" and "arrive from another state". That is the whole method.

**With a fee per transaction (714):** two states.

```cpp
// Any number of transactions with a fee per transaction (LeetCode 714).
long long maxProfitFee(const std::vector<int>& prices, int fee) {
    long long hold = -(long long)prices[0];            // best balance while holding a share
    long long cash = 0;                                // best balance while holding nothing
    for (size_t i = 1; i < prices.size(); ++i) {
        cash = std::max(cash, hold + prices[i] - fee); // sell today (pay the fee), or stay out
        hold = std::max(hold, cash - prices[i]);       // buy today, or keep holding
    }
    return cash;
}
```

Trace on `{1, 3, 2, 8, 4, 9}` with fee 2:

| day | price | cash | hold |
|---|---|---|---|
| 0 | 1 | 0 | −1 |
| 1 | 3 | 0 | −1 |
| 2 | 2 | 0 | −1 |
| 3 | 8 | **5** (sell the share bought at 1: 8 − 1 − 2) | −1 |
| 4 | 4 | 5 | **1** (buy at 4 with 5 in hand) |
| 5 | 9 | **8** (sell: 1 + 9 − 2) | 1 |

The answer is 8.

**With a cooldown (309):** you may not buy the day after you sell. Add a third state, `sold` (just sold today), whose only exit is to `idle` (nothing held, free to buy) a day later:

```cpp
// Any number of transactions, but no buying on the day after a sale (LeetCode 309).
long long maxProfitCooldown(const std::vector<int>& prices) {
    long long hold = -(long long)prices[0];            // holding a share
    long long sold = LLONG_MIN / 4;                    // sold today (impossible on day 0)
    long long idle = 0;                                // holding nothing and free to buy tomorrow
    for (size_t i = 1; i < prices.size(); ++i) {
        long long newHold = std::max(hold, idle - prices[i]);
        long long newSold = hold + prices[i];
        long long newIdle = std::max(idle, sold);      // yesterday's sale becomes tradable again today
        hold = newHold;
        sold = newSold;
        idle = newIdle;
    }
    return std::max(idle, sold);
}
```

The three new values are computed into temporaries first, because each reads the *old* values of the others. Trace on `{1, 2, 3, 0, 2}`:

| day | price | hold | sold | idle |
|---|---|---|---|---|
| 0 | 1 | −1 | impossible | 0 |
| 1 | 2 | −1 | 1 | 0 |
| 2 | 3 | −1 | 2 | 1 |
| 3 | 0 | 1 | −1 | 2 |
| 4 | 2 | 1 | **3** | 2 |

The answer is 3: buy at 1, sell at 2, rest, buy at 0, sell at 2. (`hold = 1` on day 3 means: 1 already earned, then bought at 0.)

### 7.4 At most k transactions (123, 188)

Number the transactions. `buy[j]` is the best balance while holding the share of the j-th transaction, and `sell[j]` the best balance after j transactions are complete. Buying transaction j starts from `sell[j − 1]`:

```cpp
// At most k transactions (LeetCode 188; LeetCode 123 is k = 2).
long long maxProfitAtMostK(int k, const std::vector<int>& prices) {
    if (2 * (long long)k >= (long long)prices.size()) return maxProfitUnlimited(prices);   // k no longer limits anything
    std::vector<long long> buy(k + 1, LLONG_MIN / 4);  // buy[j]: best balance while holding the share of transaction j
    std::vector<long long> sell(k + 1, 0);             // sell[j]: best balance with j transactions completed
    for (int price : prices)
        for (int j = 1; j <= k; ++j) {
            buy[j] = std::max(buy[j], sell[j - 1] - price);
            sell[j] = std::max(sell[j], buy[j] + price);
        }
    return sell[k];
}
```

O(n × k) time, O(k) space. On `{3, 3, 5, 0, 0, 3, 1, 4}` with `k = 2`:

| day | price | buy1 | buy2 | sell1 | sell2 |
|---|---|---|---|---|---|
| 0 | 3 | −3 | −3 | 0 | 0 |
| 1 | 3 | −3 | −3 | 0 | 0 |
| 2 | 5 | −3 | −3 | 2 | 2 |
| 3 | 0 | 0 | 2 | 2 | 2 |
| 4 | 0 | 0 | 2 | 2 | 2 |
| 5 | 3 | 0 | 2 | 3 | **5** |
| 6 | 1 | 0 | 2 | 3 | 5 |
| 7 | 4 | 0 | 2 | 4 | **6** |

The answer is 6 (buy at 0, sell at 3; buy at 1, sell at 4). The first line of the function matters in practice: if `2k` is at least the number of days, the limit never binds and the answer is the unlimited one, which also prevents allocating a huge array when `k` is 10⁹.

**Tip for the interview:** start the stock problems by naming the states and writing the moves. The loop body follows in a minute, and you can adapt it to any new rule (a different fee, a cooldown of two days, a daily limit) by adding a state.

---

## 8. Where this shows up in systems

- **Change detection.** The standard one-sided **CUSUM** detector for a sustained upward shift in a metric is `S = max(0, S + (x − target − allowance))`, one step per sample, and an alarm fires when `S` crosses a threshold. That is Kadane's empty-allowed loop applied to the values `x − target − allowance`. A single spike barely moves `S`; a steady drift keeps adding to it. "Find the interval with the largest accumulated excess over the baseline" is exactly maximum subarray on those values.
- **Top talkers.** A monitor cannot keep a counter for each of millions of flows. Misra-Gries keeps `k − 1` counters and still guarantees that every flow above 1/k of the traffic is among them, with counts that are too low by at most n/k. Variants and relatives of this idea (and sketch-based ones) are what traffic and telemetry systems actually use for heavy-hitter detection. "Find the flows using more than 1% of the link" is `k = 100`.
- **One pass, constant memory.** Kadane, Boyer-Moore and the stock loops all read the data once and keep O(1) numbers, so they run on a stream or on data too large for memory.
- **Mergeable summaries.** The `Node` of section 2.4 combines associatively, so partial results from different cores, shards or time ranges can be merged later. Misra-Gries summaries can be merged as well: add the counters of matching values, subtract the k-th largest counter from all of them and drop those that are no longer positive, and the same error guarantee holds (the "Mergeable Summaries" paper by Agarwal and others proves this). That is what lets each shard or switch keep its own summary and a collector combine them.

---

## 9. Choosing the tool

| The question asks for | Use |
|---|---|
| The best contiguous run (sum), array may be negative | Kadane |
| The best contiguous run (product) | Kadane with max and min |
| The same on a circle | Kadane twice, and total − min |
| Best run with a length limit | prefix sums (and a deque for a range of lengths) |
| **Count** the subarrays with a given sum | prefix sums and a hash map (tutorial 04) |
| **Longest or shortest** subarray with a monotone condition (positive numbers) | sliding window (tutorial 03) |
| The element that appears more than n/2 (n/k) times | Boyer-Moore (Misra-Gries) |
| Best profit under trading rules | named states, best value per state |
| Same as above but with updates | segment tree of `Node` |

---

## 10. Mistakes that cost points

1. **`best = 0` when the subarray must be non-empty.** All-negative arrays return a wrong 0.
2. **`int` for sums and products.** Use `long long` and say what you assume about the sizes.
3. **Product: keeping only the maximum.** A negative number needs the minimum too, and the swap must happen *before* the two updates.
4. **Circular: ignoring the all-negative array**, or returning `total − min` when the minimum is the whole array.
5. **Boyer-Moore without the second pass** when the problem does not promise a majority.
6. **Misra-Gries with `k` counters instead of `k − 1`**, or trusting the counts: they are under-estimates and the candidates need confirming.
7. **Stock DP: reading a value that was already updated today.** Compute the new states into temporaries, or order the updates so each reads yesterday's value.
8. **"At most k" and "exactly k" mixed up.** In the stock problems, doing nothing is always allowed.
9. **Returning indices: updating the start in the wrong order.** Restart the run first, then add the element, then compare with the best.
10. **Subarray versus subsequence.** A subarray is contiguous; the best *subsequence* sum is just the sum of the positive numbers.

---

## 11. Questions you will be asked

**Explain Kadane's algorithm and prove it.** The best subarray ending at `i` is either `a[i]` or the best one ending at `i − 1` plus `a[i]`. Carry that one number, and take the maximum over all ends. O(n) time, O(1) space.

**What if all the numbers are negative?** The answer is the largest single element, which is why `best` starts at `a[0]`. The empty-subarray variant returns 0.

**Return the subarray as well.** Track where the current run started, restart when the running sum is negative, and record the start and end when you beat the best.

**The array changes between queries. Now what?** A segment tree whose nodes hold (total, best prefix, best suffix, best subarray).

**Why does the product version need the minimum?** A negative factor turns the smallest product into the largest.

**How do you handle the circular array?** Max is either the ordinary Kadane or `total` minus the minimum subarray; special-case the all-negative array.

**Majority element in O(1) space? Why does it work? What if there is none?** Boyer-Moore: pairs of distinct values cancel, and a majority cannot be cancelled completely. If a majority is not guaranteed, verify the candidate with a second pass.

**Elements that occur more than n/3 times?** At most two exist: Misra-Gries with two counters and a verification pass. In general k − 1 counters.

**Stock problem with a fee, a cooldown, or k transactions?** Name the states (cash, hold, sold), write the moves between them, and carry the best balance per state.

**Why is the stock problem with unlimited transactions greedy?** A profit over several days is the sum of the daily rises, so taking every rise is optimal.

**How would you run Kadane on a huge file or in parallel?** One pass with O(1) memory for the stream; for parallel, summarise blocks as `Node` and combine them, because `combine` is associative.

---

## 12. Problems

**How to use this section.** Try each problem for 15 minutes (easy) or 30 minutes (medium) before reading anything. If you are stuck, read only the **How** lines, then try again, and only then open the answer at the bottom. Log each attempt in the [practice log](../practice_log.md). First ask: is it a best run, a count, a frequency question, or a rule-based trading problem?

### On paper

**Problem A: Trace it.** Run Kadane on `{5, −9, 6, −2, 3, −7, 4}` in a table with the columns `cur` and `best`. Which subarray is the answer? Then check the sum with the prefix array of tutorial 04.
*Where:* paper. *How:* start with `cur = best = 5`, then apply `max(a[i], cur + a[i])` once per element.

**Problem B: Pick the method.** For each, say what you would use and why.
(1) The largest sum of a non-empty subarray in an array of integers that may be negative.
(2) The same, on a circle.
(3) The number of subarrays whose sum is exactly K.
(4) Is there an element that makes up more than half of a stream of a billion numbers, with memory for 10 numbers?
(5) The most profit from trading with a fee of 2 per transaction.
(6) The largest product of a contiguous run, in an array that contains zeros and negative numbers.
*Where:* paper. *How:* use the table in section 9.

**Problem C: The circle by hand.** (a) For `{9, −4, −7, 9}` compute the total, the maximum non-wrapping sum, the minimum subarray sum, and the circular answer. (b) Do the same for `{−3, −2, −3}`. Explain why (b) is the special case.
*Where:* paper. *How:* section 5, with the two running values `curMax` and `curMin`.

**Problem D: Voting by hand.** Run the Boyer-Moore loop on `{3, 3, 4, 2, 4, 4, 2, 4, 4}` and write the candidate and count after each element. At the end, is `count` equal to the number of occurrences of the candidate? Why not?
*Where:* paper. *How:* section 6.1 table; compare the final `count` with a direct count of the 4s.

### In code

**Problem 1: Maximum Subarray** (LeetCode 53, medium)
*Where:* leetcode.com/problems/maximum-subarray. *Goal:* the largest sum of a non-empty subarray. Then, as the follow-up, return the subarray's indices.
*How:* section 2. Initialise with the first element.

**Problem 2: Maximum Product Subarray** (LeetCode 152, medium)
*Where:* leetcode.com/problems/maximum-product-subarray. *Goal:* the largest product of a non-empty subarray.
*How:* section 4: carry the largest and smallest product ending here and swap on a negative number.

**Problem 3: Maximum Sum Circular Subarray** (LeetCode 918, medium)
*Where:* leetcode.com/problems/maximum-sum-circular-subarray. *Goal:* the largest sum of a non-empty subarray of the circular array.
*How:* section 5. Compute the maximum and minimum subarrays in one pass and handle the all-negative case.

**Problem 4: Majority Element** (LeetCode 169, easy)
*Where:* leetcode.com/problems/majority-element. *Goal:* the element that appears more than n/2 times (guaranteed to exist), in O(n) time and O(1) space.
*How:* section 6.1. No second pass is needed because the majority is guaranteed.

**Problem 5: Best Time to Buy and Sell Stock II** (LeetCode 122, medium)
*Where:* leetcode.com/problems/best-time-to-buy-and-sell-stock-ii. *Goal:* the maximum profit with any number of transactions.
*How:* section 7.2. Add every rise.

**More practice, once these feel easy:** Majority Element II (229, section 6.2), Best Time to Buy and Sell Stock with Cooldown (309), with Transaction Fee (714), III (123) and IV (188), Maximum Subarray Sum with One Deletion (1186), Maximum Absolute Sum of Any Subarray (1749), Longest Turbulent Subarray (978), Max Sum of Rectangle No Larger Than K (363, hard). On CSES: Maximum Subarray Sum and Maximum Subarray Sum II. NeetCode's "Greedy" and "Arrays & Hashing" lists contain 53, 918 and 169 with video walkthroughs.

---

## Answers

Try before reading. LeetCode wraps these functions in `class Solution { public: ... };` and passes `vector<int>&`; the logic is the same.

### Problem A

| i | a[i] | cur | best |
|---|---|---|---|
| 0 | 5 | 5 | 5 |
| 1 | −9 | −4 | 5 |
| 2 | 6 | 6 (start fresh) | 6 |
| 3 | −2 | 4 | 6 |
| 4 | 3 | 7 | **7** |
| 5 | −7 | 0 | 7 |
| 6 | 4 | 4 | 7 |

The answer is 7: the subarray `6, −2, 3` (indices 2 to 4). Check: `P = {0, 5, −4, 2, 0, 3, −4, 0}`, so `P[5] − P[2] = 3 − (−4) = 7`, and 7 is the largest difference `P[r + 1] − P[l]` with `l` before `r + 1`.

### Problem B

1. **Kadane.** O(n) time, O(1) space.
2. **Kadane twice** (max and min), answer `max(maxSub, total − minSub)`, special-casing the all-negative array.
3. **Prefix sums and a hash map** (tutorial 04). A window or Kadane does not count subarrays.
4. **Boyer-Moore** with one counter: one pass, two variables, and a second pass if the answer must be verified (or a second scan of the stream).
5. **Two states** (cash and hold) with the fee paid on selling, section 7.3.
6. **Largest and smallest product ending here**, or the two-ended scan; zeros restart the run.

### Problem C

(a) `{9, −4, −7, 9}`: total = 7. The best non-wrapping run is the single `9` (9; the others are lower). The minimum subarray is `−4, −7 = −11`. Circular answer: `max(9, 7 − (−11)) = max(9, 18) = 18`: the two nines are neighbours around the circle.
(b) `{−3, −2, −3}`: the maximum is −2; the minimum is the whole array (−8) and the total is −8, so `total − min = 0`, which would mean choosing nothing. Since every element is negative, the answer is −2.

### Problem D

| x | candidate | count |
|---|---|---|
| 3 | 3 | 1 |
| 3 | 3 | 2 |
| 4 | 3 | 1 |
| 2 | 3 | 0 |
| 4 | 4 | 1 |
| 4 | 4 | 2 |
| 2 | 4 | 1 |
| 4 | 4 | 2 |
| 4 | 4 | 3 |

The candidate is 4 (and 4 does occur five times out of nine). The final `count` is 3, **not** 5: `count` is the number of votes left after cancelling, not the frequency. To know the frequency you need the second pass.

### Problem 1: Maximum Subarray

The function `maxSubarraySum` of section 2 is the answer. For `{1}` it returns 1; for `{5, 4, −1, 7, 8}` it returns 23. For the follow-up use `maxSubarrayRange`, which returns `l` and `r`. O(n) time, O(1) space.

### Problem 2: Maximum Product Subarray

The function `maxProductSubarray` of section 4 is the answer. For `{2, 3, −2, 4}` it returns 6; for `{−2, 0, −1}` it returns 0. The input guarantee (the answer fits in 32 bits) is what makes `long long` safe here. O(n) time, O(1) space.

### Problem 3: Maximum Sum Circular Subarray

The function `maxCircularSubarray` of section 5 is the answer. For `{1, −2, 3, −2}` it returns 3; for `{5, −3, 5}` it returns 10; for `{−3, −2, −3}` it returns −2. O(n) time, O(1) space.

### Problem 4: Majority Element

Because a majority is guaranteed, `majorityCandidate` of section 6.1 is the whole solution: LeetCode's `int majorityElement(vector<int>& nums)` is `return majorityCandidate(nums);` (the function `majorityElement` of section 6.1 adds the verification pass). For `{3, 2, 3}` it returns 3. O(n) time, O(1) space.

### Problem 5: Best Time to Buy and Sell Stock II

The function `maxProfitUnlimited` of section 7.2 is the answer. For `{1, 2, 3, 4, 5}` it returns 4 (four daily rises of 1); for `{7, 6, 4, 3, 1}` it returns 0. O(n) time, O(1) space.

---

## Cheat sheet

- **Best run ending here** is the engine: `cur = max(a[i], cur + a[i])`, answer = largest `cur`. O(n), O(1).
- **Kadane:** start with `a[0]` for non-empty; `max(0, ...)` for empty allowed. Restart when the running sum is negative. Track the start to return the subarray.
- **Segment form:** `Node{sum, prefix, suffix, best}`, `combine` in O(1); the answer for updates and for parallel blocks.
- **Grid rectangles:** fix two rows, add them into one array, Kadane along it: O(m² n).
- **One deletion:** two states, `noDel` and `oneDel`; update `oneDel` first.
- **Product:** carry max and min, swap on a negative, zeros restart.
- **Circular:** `max(kadaneMax, total − kadaneMin)`; if all negative, `kadaneMax`.
- **Majority (> n/2):** `if (count == 0) candidate = x; count += (x == candidate) ? 1 : -1;` then verify if not guaranteed. **> n/k:** k − 1 counters, decrement all when full, verify; counts are under-estimates by at most n/k.
- **Stock:** 121 Kadane on changes; 122 sum of rises; fee: cash and hold; cooldown: hold, sold, idle; at most k: `buy[j]`, `sell[j]`, and `k >= n/2` means unlimited.
- **Systems:** Kadane's empty-allowed loop is the CUSUM change detector; Misra-Gries finds top talkers with k − 1 counters; mergeable summaries parallelise.

---

## Sources

- Counts, outputs and timings in this tutorial were produced by running the code shown in it on the machine described in the text. Times are that machine's, not yours.
- The algorithms and data structures are standard; for further reading see the "Extra practice and resources" section of the [area plan](00_plan.md), which lists the textbook chapters and free references for this area. The explanations and code are my own.
- LeetCode numbers and titles are from memory; check a number before you rely on it.

**Next:** tutorial 06, Matrix Traversal and In-Place Tricks. Say "next" when you are ready.
