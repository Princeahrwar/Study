# DSA 06.04: Divide and Conquer and Recurrences

*Part of the [Recursion and backtracking plan](00_plan.md) · Time: about 8 hours (4 reading, 4 problems) · You need: [Recursion fundamentals](01_recursion_fundamentals.md) (recursion trees, depth against work, call counts) and [Sorting algorithms inside](../03_binary_search_and_sorting/04_sorting_algorithms_inside.md) (merge sort and quicksort) · Related: [Constraint backtracking](03_constraint_backtracking.md) · No companion file: every listing is in this tutorial.*

By the end of this tutorial you can:

- split a problem into independent parts, solve them recursively and combine the answers, and say what the combine step must achieve;
- write the recurrence of a recursive algorithm from its code and solve it with the recursion-tree method and the master theorem;
- count inversions, smaller elements after each element and reverse pairs with a merge sort, and see why the merge makes counting linear;
- solve maximum subarray, selection (kth largest), closest pair of points and big-number multiplication by divide and conquer, and state each cost;
- tell when divide and conquer is the wrong tool (overlapping parts, unbalanced splits) and where it parallelises.

**Where the numbers come from.** Every listing was compiled with `-Wall -Wextra` and tested: 10,041 checks passed under AddressSanitizer with UBSan (GCC 13.3), against references such as `std::sort`, quadratic brute force for every counting problem, Kadane's algorithm, a full sort for selection, every order of evaluation for the parenthesis problem, quadratic closest pair, and for Karatsuba `unsigned __int128` plus Python's big integers. The recurrence tables were computed exactly by a short script. Call counts and comparison counts are exact and the same on every machine. The timings were measured on one machine: an x86-64 VM with 2 cores at 2.10 GHz, Ubuntu 24.04, GCC 13.3 with `-O2`. The statements of the LeetCode problems are summarised from memory; titles were checked against pages that list them (see Sources), and no difficulty labels are given because none could be verified.

All listings need these headers: `<algorithm>`, `<cstdint>`, `<random>`, `<string>`, `<utility>` and `<vector>`. They use `using Ints = std::vector<int>;`.

---

## 1. Divide, conquer, combine

### 1.1 The pattern

Tutorial 01 wrote recursions with one smaller call (`sumTo`) and with overlapping calls (`fib`). **Divide and conquer** is the pattern with several smaller calls on **independent parts** of the input, usually about half each:

1. **Divide.** Split the input into parts that do not overlap.
2. **Conquer.** Solve each part by the same function; a part small enough is solved directly (the base case).
3. **Combine.** Build the answer for the whole from the answers for the parts.

Almost all the thinking is in the combine step. If it is cheap, the algorithm is fast; if it is clever, it can answer a question that looks global. Merge sort is the model: the combine step (merge) turns two sorted halves into one sorted whole in linear time.

```cpp
// Sort a[lo, hi). Take from the left half when the values are equal (stable).
void mergeSort(Ints& a, Ints& tmp, int lo, int hi, long long& cmp) {
    if (hi - lo < 2) return;                               // base case: 0 or 1 element
    int mid = lo + (hi - lo) / 2;
    mergeSort(a, tmp, lo, mid, cmp);                       // divide
    mergeSort(a, tmp, mid, hi, cmp);
    int i = lo, j = mid, k = lo;                           // combine: merge two sorted halves
    while (i < mid && j < hi) { ++cmp; tmp[k++] = (a[j] < a[i]) ? a[j++] : a[i++]; }
    while (i < mid) tmp[k++] = a[i++];
    while (j < hi) tmp[k++] = a[j++];
    std::copy(tmp.begin() + lo, tmp.begin() + hi, a.begin() + lo);
}
```

The listing sorts `a[lo, hi)` and counts comparisons. The three parts of the pattern are marked in the comments. A check by induction, as in tutorial 01: if both halves come back sorted, then the merge returns a sorted whole; and every call has a smaller range, down to the base case.

### 1.2 What the splits cost

The recursion tree of merge sort has `log2 n` levels (the range halves each time). At every level the merges together touch each element once. So the work is about `n log2 n`, and the depth, which is the stack space, is only `log2 n`. Comparisons counted by the listing for `n = 1024`:

| Input | Comparisons |
|---|---|
| Already sorted | 5,120 |
| Reversed | 5,120 |
| Random | 8,933 |
| Worst-case bound `n * ceil(log2 n) - 2^ceil(log2 n) + 1` | 9,217 |

Sorted and reversed input need `(n / 2) log2 n = 512 * 10 = 5,120` comparisons, because in every merge one side runs out first. The bound held for every random test array. The split is the point: with a **balanced** split the depth is `log n`; if the split were `n - 1` and `1` (as in a quicksort with bad pivots), the depth would be `n` and the work `n^2`.

---

## 2. Recurrences

### 2.1 From code to recurrence

Let `T(n)` be the time on an input of size `n`. A divide-and-conquer function that makes `a` calls on parts of size `n / b` and spends `f(n)` on the divide and combine steps has

```text
T(n) = a * T(n / b) + f(n)
```

Merge sort: two calls on halves and a linear merge, so `T(n) = 2 T(n/2) + n`. Binary search: one call on a half, constant work, `T(n) = T(n/2) + 1`. Write the recurrence first; then solve it.

### 2.2 The recursion-tree method

Draw the tree and add up each level, as in tutorial 01. Level `i` has `a^i` calls, each on size `n / b^i`, so the level costs `a^i * f(n / b^i)`. The total is the sum over the levels, down to the leaves at `i = log_b n`, where there are `a^(log_b n) = n^(log_b a)` of them.

For merge sort `a = b = 2` and `f(n) = n`: every level costs `2^i * (n / 2^i) = n`, and there are `log2 n` levels, so the total is `n log2 n` (plus `n` for the leaves). For a recurrence like `3 T(n/2) + n` the levels cost `n`, `1.5 n`, `2.25 n`, ...: they grow geometrically, so the **leaves dominate**. For `2 T(n/2) + n^2` the levels cost `n^2`, `n^2 / 2`, `n^2 / 4`, ...: they shrink geometrically, so the **root dominates**. Three cases, and they are the master theorem.

### 2.3 The master theorem

For `T(n) = a T(n/b) + Θ(n^c)` with `a >= 1`, `b > 1`, `c >= 0`, let `e = log_b a`. Compare the cost of the leaves, `n^e`, with the cost of the root, `n^c`:

| Case | Condition | Result | Who dominates |
|---|---|---|---|
| 1 | `c < e` | `Θ(n^e)` | the leaves |
| 2 | `c = e` | `Θ(n^c log n)` | every level the same |
| 3 | `c > e` | `Θ(n^c)` | the root |

Rounding does not matter: `n/2` may be `floor` or `ceil` (an odd length splits into `k` and `k + 1`) and the result is the same. (This is the form for a polynomial `f`, which is all you need in interviews; the general statement is in CLRS, chapter 4.)

| Algorithm | Recurrence | `e` | Case | Result |
|---|---|---|---|---|
| Binary search | `T(n/2) + 1` | 0 | 2 (`c = 0`) | `log n` |
| Maximum subarray, section 4 | `2 T(n/2) + 1` | 1 | 1 | `n` |
| Merge sort, counting inversions, closest pair | `2 T(n/2) + n` | 1 | 2 | `n log n` |
| Quickselect with perfect halves, section 5 | `T(n/2) + n` | 0 | 3 | `n` |
| Karatsuba, section 8 | `3 T(n/2) + n` | 1.585 | 1 | `n^1.585` |
| Splitting each factor in two, four products, section 8 | `4 T(n/2) + n` | 2 | 1 | `n^2` |
| Strassen's matrix product (from memory) | `7 T(n/2) + n^2` | 2.807 | 1 | `n^2.807` |

To see the cases in numbers, here are exact values of `T(n)` with `T(1) = 1` for `n = 2^10` and `n = 2^20`, and the ratio `T(2n) / T(n)` at `n = 2^20` (what doubling the input does):

| Recurrence | `T(2^10)` | `T(2^20)` | `T(2n) / T(n)` | Growth |
|---|---|---|---|---|
| `T(n/2) + 1` | 11 | 21 | 1.048 | `log n` |
| `2 T(n/2) + 1` | 2,047 | 2,097,151 | 2.000 | `n` |
| `T(n/2) + n` | 2,047 | 2,097,151 | 2.000 | `n` |
| `2 T(n/2) + n` | 11,264 | 22,020,096 | 2.095 | `n log n` |
| `3 T(n/2) + n` | 175,099 | 10,458,256,051 | 3.000 | `n^1.585` (doubling gives `2^1.585 = 3`) |
| `4 T(n/2) + n` | 2,096,128 | 2,199,022,206,976 | 4.000 | `n^2` |
| `2 T(n/2) + n^2` | 2,096,128 | 2,199,022,206,976 | 4.000 | `n^2` |
| `4 T(n/2) + n^2` | 11,534,336 | 23,089,744,183,296 | 4.190 | `n^2 log n` |

The ratio is the fingerprint: 2 for linear, a little over 2 for `n log n`, 3 for `n^1.585`, 4 for quadratic. Rows 6 and 7 have identical values (both are `2n^2 - n`) but not the same tree: one is leaves-heavy, the other root-heavy.

### 2.4 When the master theorem does not apply

- **Subtract, do not divide.** `T(n) = T(n-1) + n` (a quicksort with the worst pivot, or insertion sort) is `n(n+1)/2`, so `Θ(n^2)`. `T(n) = 2 T(n-1) + 1` (Tower of Hanoi, tutorial 01) is `2^n - 1`. Draw the tree and add the levels.
- **Unequal parts.** `T(n) = T(n/3) + T(2n/3) + n`: each level costs at most `n` and the depth is `log_{3/2} n`, so `n log n`.

---

## 3. Counting with a merge sort

The merge step looks at two **sorted** halves. That makes counting a question about pairs `(i, j)` with `i` in the left half and `j` in the right half much cheaper: for a given `j`, all the left elements that matter form a **prefix or a suffix** of the sorted left half, so one pass counts them all. The pairs inside a half are counted by the recursive calls, and each pair `(i, j)` has exactly one level where it is split between the two halves, so no pair is counted twice.

```cpp
// Inversions: pairs i < j with a[i] > a[j]. When the merge takes a right element, it is
// smaller than every left element that is still waiting, which is mid - i of them.
long long inversions(Ints& a, Ints& tmp, int lo, int hi) {
    if (hi - lo < 2) return 0;
    int mid = lo + (hi - lo) / 2;
    long long count = inversions(a, tmp, lo, mid) + inversions(a, tmp, mid, hi);
    int i = lo, j = mid, k = lo;
    while (i < mid && j < hi) {
        if (a[j] < a[i]) { count += mid - i; tmp[k++] = a[j++]; }
        else tmp[k++] = a[i++];
    }
    while (i < mid) tmp[k++] = a[i++];
    while (j < hi) tmp[k++] = a[j++];
    std::copy(tmp.begin() + lo, tmp.begin() + hi, a.begin() + lo);
    return count;
}

// Count of Smaller Numbers After Self (LeetCode 315): sort the indices by value and
// add up, for each left element, the right elements that were taken before it.
void smallerAfter(const Ints& a, Ints& idx, Ints& tmp, Ints& ans, int lo, int hi) {
    if (hi - lo < 2) return;
    int mid = lo + (hi - lo) / 2;
    smallerAfter(a, idx, tmp, ans, lo, mid);
    smallerAfter(a, idx, tmp, ans, mid, hi);
    int i = lo, j = mid, k = lo;
    while (i < mid || j < hi) {
        if (j == hi || (i < mid && a[idx[i]] <= a[idx[j]])) {
            ans[idx[i]] += j - mid;                        // right elements smaller than a[idx[i]]
            tmp[k++] = idx[i++];
        } else tmp[k++] = idx[j++];
    }
    std::copy(tmp.begin() + lo, tmp.begin() + hi, idx.begin() + lo);
}

// Reverse Pairs (LeetCode 493): i < j with a[i] > 2 * a[j]. Count first, with a second
// pointer over the sorted right half, and merge afterwards.
long long reversePairs(Ints& a, int lo, int hi) {
    if (hi - lo < 2) return 0;
    int mid = lo + (hi - lo) / 2;
    long long count = reversePairs(a, lo, mid) + reversePairs(a, mid, hi);
    int j = mid;
    for (int i = lo; i < mid; ++i) {
        while (j < hi && (long long)a[i] > 2LL * a[j]) ++j;
        count += j - mid;
    }
    std::inplace_merge(a.begin() + lo, a.begin() + mid, a.begin() + hi);
    return count;
}
```

**Inversions.** Pairs `i < j` with `a[i] > a[j]`. In the merge, when the next element comes from the right half (`a[j] < a[i]`, strictly), it is smaller than the `mid - i` left elements still waiting, so add `mid - i`. For `{3, 1, 2}` the answer is 2; for `{2, 4, 1, 3, 5}` it is 3 (both tested).

**Count of Smaller Numbers After Self (LeetCode 315).** For every index, how many later elements are smaller? The array itself must not move, so sort an array of **indices** by value. When the merge takes a left element, the right elements already taken are exactly the ones smaller than it (the `<=` takes the left element first on ties, so equal values do not count), so add `j - mid` to that element's answer. For `{5, 2, 6, 1}` the answer is `{2, 1, 1, 0}`.

**Reverse Pairs (LeetCode 493).** Pairs `i < j` with `a[i] > 2 * a[j]`. Here the condition is not the merge's own order, so counting is a separate pass before the merge: with both halves sorted, as `i` moves right through the left half, the first `j` that fails `a[i] > 2 * a[j]` can only move right too. Two pointers, linear. Then merge (the listing uses `std::inplace_merge`). Use `long long` for `2 * a[j]`: with `int` it overflows near `INT_MAX` (the tests include values over the whole `int` range).

The cost is `T(n) = 2 T(n/2) + n`, so `n log n`. Measured on 50,000 random integers (623,715,444 inversions):

| Method | Time |
|---|---|
| Two loops over all pairs | 501 ms |
| `inversions` | 4.2 ms |

The gap grows as `n / log n`. Typical errors: `<=` where `<` is meant (ties are not inversions), counting after the merge instead of before it (the halves are then mixed), and forgetting that the recursion must hand back **sorted** halves even when you only want a count.

---

## 4. Maximum subarray

The maximum subarray (LeetCode 53) has a one-pass solution, Kadane's algorithm, from tutorial 01 of the arrays area. Divide and conquer gives a second solution and shows how to **choose what each part reports**. The best subarray of a range is in the left half, in the right half, or crosses the middle; the crossing one is a suffix of the left half joined to a prefix of the right half. So each part returns four numbers: its sum, its best prefix, its best suffix and its best subarray.

```cpp
// Maximum Subarray by divide and conquer. Each half reports four numbers, and the best
// subarray is in the left, in the right, or crosses the middle (left suffix + right prefix).
struct Seg { long long sum, pref, suf, best; };

Seg maxSeg(const Ints& a, int lo, int hi) {                // a[lo, hi), not empty
    if (hi - lo == 1) return {a[lo], a[lo], a[lo], a[lo]};
    int mid = lo + (hi - lo) / 2;
    Seg L = maxSeg(a, lo, mid), R = maxSeg(a, mid, hi);
    return {L.sum + R.sum,
            std::max(L.pref, L.sum + R.pref),
            std::max(R.suf, R.sum + L.suf),
            std::max({L.best, R.best, L.suf + R.pref})};
}
```

For `{-2, 1, -3, 4, -1, 2, 1, -5, 4}` the left half `{-2, 1, -3, 4}` returns (sum 0, prefix 0, suffix 4, best 4) and the right half returns (sum 1, prefix 2, suffix 4, best 4); the crossing subarray is `4 + 2 = 6`, the answer (the subarray `4, -1, 2, 1`). The combine step is O(1), so `T(n) = 2 T(n/2) + 1 = Θ(n)`: the same cost as Kadane. On 1,000,000 random numbers it took 5.9 ms (best of 3), and agreed with Kadane's result.

Why bother? A summary of a range that combines associatively is exactly what a **segment tree** stores, so this is the version that answers "maximum subarray inside `[l, r]`" with updates (a later area).

---

## 5. Selection: kth largest

To find the kth largest element you do not need a full sort. **Quickselect** partitions around a pivot, like quicksort, but follows **only the side that contains the answer**. The listing partitions into three parts (less, equal, greater) in one pass, so many equal values do not cause trouble.

```cpp
// Kth Largest Element (LeetCode 215) by quickselect: partition around a random pivot into
// less / equal / greater, then continue only in the part that holds the answer.
// visits counts how many elements the partition loops look at.
int kthLargest(Ints a, int k, std::mt19937& rng, long long& visits) {
    int lo = 0, hi = (int)a.size() - 1, target = (int)a.size() - k;   // index in ascending order
    while (lo < hi) {
        int pivot = a[lo + (int)(rng() % (unsigned)(hi - lo + 1))];
        int lt = lo, i = lo, gt = hi;                      // a[lo,lt) < pivot, a[gt,hi] > pivot
        while (i <= gt) {
            ++visits;
            if (a[i] < pivot) std::swap(a[lt++], a[i++]);
            else if (a[i] > pivot) std::swap(a[i], a[gt--]);
            else ++i;
        }
        if (target < lt) hi = lt - 1;
        else if (target > gt) lo = gt + 1;
        else return pivot;
    }
    return a[lo];
}
```

(LeetCode 215.) If the pivot always split the range in half, the work would be `n + n/2 + n/4 + ...`, which is `2n`: the recurrence `T(n) = T(n/2) + n`, case 3, `Θ(n)`. The pivot is random, so the split is good *on average*; the expected cost is `Θ(n)` and the worst case, where the pivot is always at an end, is `Θ(n^2)`. Counting every element the partition loop looks at, for `n = 1,000,000` random values:

| Case | Elements visited | Per element |
|---|---|---|
| k = 1 (the maximum) | 1,518,633 | 1.52 |
| k = n / 2 (the median) | 3,573,235 | 3.57 |
| All elements equal | 1,000,000 | 1.00 |

Both answers were checked against a full sort. The median is the hardest case, as it keeps the most elements in play. All-equal input costs one pass, thanks to the three-way partition (a two-way partition would drift toward `n^2`).

Alternatives: a heap of size `k` costs `n log k` and works on a stream; `std::nth_element` is the library's selection; the median of medians gives worst-case `Θ(n)` with a large constant (not implemented here).

---

## 6. Different Ways to Add Parentheses

Given an expression such as `2*3-4*5`, compute the results of all the ways of grouping it with parentheses (LeetCode 241). Divide at **each operator**: the operator is the last one evaluated, the left side and the right side are expressions of their own, and the results are all pairs `(x, y)` combined with the operator.

```cpp
// Different Ways to Add Parentheses (LeetCode 241): split at every operator, solve both
// sides, combine every pair of results. s[lo, hi) holds digits and + - *.
Ints ways(const std::string& s, int lo, int hi, long long& calls) {
    ++calls;
    Ints out;
    for (int i = lo; i < hi; ++i) {
        char op = s[i];
        if (op != '+' && op != '-' && op != '*') continue;
        Ints L = ways(s, lo, i, calls), R = ways(s, i + 1, hi, calls);
        for (int x : L)
            for (int y : R) out.push_back(op == '+' ? x + y : op == '-' ? x - y : x * y);
    }
    if (out.empty()) out.push_back(std::stoi(s.substr(lo, hi - lo)));   // no operator: a number
    return out;
}
```

For `2-1-1` the results are `0` and `2`; for `2*3-4*5` they are `-34, -14, -10, -10, 10` (both tested). With `k` operators the number of results is the Catalan number (1, 2, 5, 14, 42, ...), the same count as the balanced parentheses of tutorial 02.

This is divide and conquer with **overlapping parts**: the sub-expression `3-4` appears in several splits. For `1+1+...+1` the calls and the number of different sub-ranges are:

| Operators `k` | Results | Calls of `ways` | Different ranges `(k+1)(k+2)/2` |
|---|---|---|---|
| 1 | 1 | 3 | 3 |
| 2 | 2 | 9 | 6 |
| 3 | 5 | 27 | 10 |
| 4 | 14 | 81 | 15 |
| 5 | 42 | 243 | 21 |
| 6 | 132 | 729 | 28 |
| 7 | 429 | 2,187 | 36 |
| 8 | 1,430 | 6,561 | 45 |

The calls are `3^k` for this input (checked for k up to 8), but only `(k+1)(k+2)/2` different ranges exist. Storing the list of results of each range (memoisation) makes each range be solved once; this turns the recursion into a dynamic programming over ranges, which is the next area. The results themselves still number `Catalan(k)`, so the output size limits the gain.

The lesson: **divide and conquer is for independent parts**. When parts overlap and there are far fewer different sub-problems than calls, memoise.

---

## 7. Closest pair of points

Given `n` points in the plane, find the two closest. All pairs: `n (n - 1) / 2` distances. Divide and conquer reaches `n log n`:

1. Sort the points by `x` once. Split at the middle point: a left and a right half.
2. Solve both halves; let `d` be the smaller of their two answers (a distance, squared in the code).
3. The closest pair overall is either one of those or a **crossing** pair, with one point on each side. A crossing pair closer than `d` has both points within `d` of the middle line, in a vertical **strip**.
4. Inside the strip, sort the points by `y`. A point only needs to be compared with the points just before it whose `y` differs by less than `d`; a packing argument shows there are at most a constant number of them (the standard proof is in CLRS, in the closest-pair section of the divide-and-conquer chapter).

The sort by `y` inside the strip would cost `n log n` at every level, and that would give `n log^2 n`. The listing avoids that: every call also **returns its range sorted by `y`**, by merging the two sorted halves, which is linear, so `T(n) = 2 T(n/2) + n`, case 2.

```cpp
// Closest pair of points. Sort by x once. Each call returns the smallest squared distance in
// its range and leaves the range sorted by y, so the strip around the middle line is cheap.
struct P { long long x, y; };

long long dist2(const P& a, const P& b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y); }

long long closestRec(std::vector<P>& p, std::vector<P>& tmp, int lo, int hi, long long& evals) {
    if (hi - lo <= 3) {
        long long best = INT64_MAX;
        for (int i = lo; i < hi; ++i)
            for (int j = i + 1; j < hi; ++j) { ++evals; best = std::min(best, dist2(p[i], p[j])); }
        std::sort(p.begin() + lo, p.begin() + hi, [](const P& a, const P& b) { return a.y < b.y; });
        return best;
    }
    int mid = lo + (hi - lo) / 2;
    long long midx = p[mid].x;
    long long d = std::min(closestRec(p, tmp, lo, mid, evals), closestRec(p, tmp, mid, hi, evals));
    std::merge(p.begin() + lo, p.begin() + mid, p.begin() + mid, p.begin() + hi, tmp.begin() + lo,
               [](const P& a, const P& b) { return a.y < b.y; });
    std::copy(tmp.begin() + lo, tmp.begin() + hi, p.begin() + lo);
    std::vector<P> strip;                                  // points within distance sqrt(d) of the line
    for (int i = lo; i < hi; ++i) {
        if ((p[i].x - midx) * (p[i].x - midx) >= d) continue;
        for (int j = (int)strip.size() - 1; j >= 0 && (p[i].y - strip[j].y) * (p[i].y - strip[j].y) < d; --j) {
            ++evals;
            d = std::min(d, dist2(p[i], strip[j]));
        }
        strip.push_back(p[i]);
    }
    return d;
}

long long closestPair(std::vector<P> pts, long long& evals) {   // at least 2 points
    std::sort(pts.begin(), pts.end(), [](const P& a, const P& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    std::vector<P> tmp(pts.size());
    return closestRec(pts, tmp, 0, (int)pts.size(), evals);
}
```

Counting the distance computations (random points with coordinates up to a billion):

| Points | Distance computations | All pairs | Time |
|---|---|---|---|
| 1,000 | 1,130 | 499,500 | 0.2 ms |
| 100,000 | 142,526 | 4,999,950,000 | 23 ms |
| 1,000,000 | 1,215,175 | 499,999,500,000 | 294 ms |

At a million points the search does about 1.2 distance computations per point, against half a million for the all-pairs method. The tests compared the answers with the quadratic method on 1,500 sets with many duplicates, collinear points and large coordinates, and the code uses squared integer distances, so there is no rounding.

---

## 8. Multiplying big numbers: Karatsuba

Multiplying two `n`-digit numbers with the school method takes `n^2` digit products. Split each number in two halves, `a = a1 * B + a0` and `b = b1 * B + b0` with `B = 10^m`. Then

```text
a * b = a1 b1 * B^2 + (a1 b0 + a0 b1) * B + a0 b0
```

That is four half-size products, `T(n) = 4 T(n/2) + n`, which is `Θ(n^2)`: no gain (case 1 of the table in section 2.3). **Karatsuba's idea** is that the middle term needs only one product:

```text
a1 b0 + a0 b1 = (a0 + a1)(b0 + b1) - a0 b0 - a1 b1
```

So three half-size products suffice: `T(n) = 3 T(n/2) + n`, `Θ(n^1.585)`. Example, `47 * 83` with `B = 10`: `a1 = 4, a0 = 7, b1 = 8, b0 = 3`; `z2 = 4 * 8 = 32`, `z0 = 7 * 3 = 21`, `z1 = (4 + 7)(8 + 3) - 21 - 32 = 121 - 53 = 68`, and the result is `32 * 100 + 68 * 10 + 21 = 3901`.

```cpp
// Multiply two non-negative numbers given as decimal digits, least significant first.
using Digits = std::vector<int>;

static Digits trim(Digits a) { while (a.size() > 1 && a.back() == 0) a.pop_back(); return a; }

static Digits addD(const Digits& a, const Digits& b) {
    Digits r; int carry = 0;
    for (size_t i = 0; i < std::max(a.size(), b.size()) || carry; ++i) {
        int s = carry + (i < a.size() ? a[i] : 0) + (i < b.size() ? b[i] : 0);
        r.push_back(s % 10); carry = s / 10;
    }
    return trim(r);
}

static Digits subD(const Digits& a, const Digits& b) {     // a >= b
    Digits r; int borrow = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        int s = a[i] - borrow - (i < b.size() ? b[i] : 0);
        borrow = s < 0; r.push_back(s + 10 * borrow);
    }
    return trim(r);
}

static Digits shiftD(Digits a, size_t m) { if (a.size() == 1 && a[0] == 0) return a; a.insert(a.begin(), m, 0); return a; }

// Schoolbook: every digit of a times every digit of b. ops counts the digit products.
Digits mulSchool(const Digits& a, const Digits& b, long long& ops) {
    std::vector<int> r(a.size() + b.size() + 1, 0);
    for (size_t i = 0; i < a.size(); ++i)
        for (size_t j = 0; j < b.size(); ++j) { ++ops; r[i + j] += a[i] * b[j]; }
    int carry = 0;
    for (size_t i = 0; i < r.size(); ++i) { int s = r[i] + carry; r[i] = s % 10; carry = s / 10; }
    return trim(r);
}

// Karatsuba: three half-size products instead of four. Below cutoff digits, use schoolbook.
Digits mulKaratsuba(const Digits& a, const Digits& b, long long& ops, size_t cutoff = 16) {
    if (std::min(a.size(), b.size()) <= cutoff) return mulSchool(a, b, ops);
    size_t m = std::max(a.size(), b.size()) / 2;
    auto lowPart = [&](const Digits& x) { return trim(Digits(x.begin(), x.begin() + std::min(m, x.size()))); };
    auto highPart = [&](const Digits& x) { return x.size() > m ? trim(Digits(x.begin() + m, x.end())) : Digits{0}; };
    Digits a0 = lowPart(a), a1 = highPart(a), b0 = lowPart(b), b1 = highPart(b);
    Digits z0 = mulKaratsuba(a0, b0, ops, cutoff);
    Digits z2 = mulKaratsuba(a1, b1, ops, cutoff);
    Digits z1 = subD(subD(mulKaratsuba(addD(a0, a1), addD(b0, b1), ops, cutoff), z0), z2);
    return addD(addD(shiftD(z2, 2 * m), shiftD(z1, m)), z0);
}
```

Below `cutoff` digits the code switches to the school method; a divide-and-conquer algorithm nearly always has a size below which the simple method wins. `ops` counts the digit products. The two methods gave the same result on 600 random pairs, and the product of two 200-digit numbers of nines was checked exactly; three 300-digit products were checked against Python.

| Digits `n` | School: `n^2` digit products | Karatsuba (cutoff 16) | Fewer by | School time | Karatsuba time |
|---|---|---|---|---|---|
| 64 | 4,096 | 2,306 | 1.8 times | under 0.1 ms | under 0.1 ms |
| 256 | 65,536 | 20,142 | 3.3 times | under 0.1 ms | 0.1 ms |
| 1,024 | 1,048,576 | 182,004 | 5.8 times | 0.5 ms | 1.2 ms |
| 4,096 | 16,777,216 | 1,632,811 | 10.3 times | 7.0 ms | 10.5 ms |

The operation count falls as predicted, but **in this code Karatsuba is slower in time** at every size up to 4,096 digits. Each call builds several small vectors (splits, sums, differences), and a digit product is only one cheap instruction, so the extra additions and the allocations cost more than the saved products. Real libraries use large words as digits (not base 10) and switch to Karatsuba only above a threshold of many words (from memory: GMP's thresholds are tens of words, and above a larger threshold it uses FFT methods). The point is a general one: **an operation count is not a running time**; the crossover depends on the constants, and a good implementation measures it.

---

## 9. Where it parallelises

The two recursive calls work on **disjoint** parts and need nothing from each other, so they can run on two threads (fork-join), down to a size below which the thread overhead costs more than the work. The combine step runs after both halves are done, so it is the sequential part: the top-level merge handles all `n` elements by itself, which is why merge sort scales less than perfectly with cores. I have not measured a parallel version here.

---

## 10. Common mistakes

| Mistake | What happens | Fix |
|---|---|---|
| Unbalanced split (`n - 1` and `1`) | Depth `n`, work `n^2`: the divide and conquer is gone | Split in the middle; for pivots, randomise |
| Counting after the merge | The halves are mixed; the pointer argument fails | Count between the recursive calls and the merge |
| `<=` instead of `<` in the inversion merge | Ties counted as inversions | Take the right element only when strictly smaller |
| `2 * a[j]` in `int` | Overflow near `INT_MAX` | Use `2LL * a[j]` |
| Two-way partition with many equal values | Quickselect degrades to `n^2` | Three-way partition |
| Divide and conquer on overlapping parts | Exponential calls | Count the different sub-problems and memoise |
| Believing a lower operation count is faster | A slower program (section 8) | Measure with the real data sizes |

---

## 11. Questions to answer out loud

1. What are the three steps of divide and conquer, and where does the cleverness usually live?
2. Write the recurrence of merge sort and solve it with the recursion tree.
3. State the three cases of the master theorem for `T(n) = a T(n/b) + n^c`.
4. Why is `T(n) = 3 T(n/2) + n` case 1, and what does it give? What does the doubling ratio tell you?
5. Why can counting inversions be done during the merge, and which pairs does each level count?
6. In Reverse Pairs, why must the counting pass come before the merge?
7. What four numbers does each part of the maximum subarray return, and why?
8. Why is the quickselect cost `Θ(n)` on average but `Θ(n^2)` in the worst case? Why a three-way partition?
9. Why does `ways` for `1+1+...+1` make `3^k` calls while only `(k+1)(k+2)/2` ranges exist?
10. How does the closest-pair code avoid an extra `log n` factor?
11. How does Karatsuba save a product, and why was it slower in the measurement here?

---

## 12. Problems

Log every attempt in [`../practice_log.md`](../practice_log.md). Titles and numbers were checked (see Sources); statements are summarised from memory, so read the real statement first.

### 12.1 On paper

**Problem A.** Solve with the master theorem and name the case: `T(n) = 3 T(n/3) + n`; `T(n) = 4 T(n/2) + n^2`; `T(n) = 2 T(n/2) + n^2`; `T(n) = T(n/2) + n`.

*Where:* paper. *Goal:* four results. *How:* compute `e = log_b a`, compare with `c`.

**Problem B.** Write the recurrence of each: `maxSeg`, `inversions`, a binary search that copies the half it keeps into a new array, and a recursion that calls itself on `n - 1` twice.

*Where:* paper. *Goal:* four recurrences and their growth. *How:* count the calls and the work outside the calls.

**Problem C.** Run `inversions` by hand on `{3, 1, 2}` and on `{2, 4, 1, 3, 5}`. At which merge is each inversion counted?

*Where:* paper. *Goal:* 2 and 3. *How:* split in halves of sizes `floor(n/2)` and the rest, and add `mid - i` at each right-element step.

**Problem D.** Compute the Reverse Pairs count (`a[i] > 2 * a[j]`) of `{1, 3, 2, 3, 1}` and of `{2, 4, 3, 5, 1}`.

*Where:* paper. *Goal:* two counts. *How:* list the pairs, then check them with the two-pointer pass.

**Problem E.** Draw the `maxSeg` tree for `{-2, 1, -3, 4, -1, 2, 1, -5, 4}` down to the leaves, and fill in the four numbers at the root.

*Where:* paper. *Goal:* the root's four numbers. *How:* the halves are `{-2, 1, -3, 4}` and the rest.

**Problem F.** Multiply `56 * 78` with Karatsuba's three products and check against the school method.

*Where:* paper. *Goal:* the product. *How:* `z0 = a0 b0`, `z2 = a1 b1`, `z1 = (a0 + a1)(b0 + b1) - z0 - z2`.

### 12.2 In your editor

For each, write a brute force first, then the divide-and-conquer version, and compare on random inputs.

1. **Maximum Subarray (53).** Where: LeetCode. Goal: the maximum sum. How: the four-number version, then compare with Kadane.
2. **Kth Largest Element in an Array (215).** Where: LeetCode. Goal: the kth largest. How: quickselect with three-way partition; compare with a full sort.
3. **Count of Smaller Numbers After Self (315).** Where: LeetCode. Goal: a count per index. How: merge sort on indices.
4. **Reverse Pairs (493).** Where: LeetCode. Goal: the number of pairs. How: count with two pointers before each merge; use `long long`.
5. **Different Ways to Add Parentheses (241).** Where: LeetCode. Goal: all results. How: split at each operator; then memoise by range and count calls.
6. **Multiply Strings (43).** Where: LeetCode. Goal: the product of two digit strings. How: the school method is what the problem expects; try Karatsuba for very long inputs and measure.
7. **Closest pair of points.** Where: your editor. Goal: the smallest distance of `n` points. How: the listing; compare with the quadratic method on small random sets with duplicates.

**Extra.** Run the two halves of `mergeSort` on two threads (for example with `std::async`) down to a depth of 2 and measure against the single-threaded version for 10 million integers. Explain the speed-up you get from the sequential top merge.

---

## Answers

**Problem A.** `T(n) = 3 T(n/3) + n`: `e = 1 = c`, case 2, `Θ(n log n)`. `4 T(n/2) + n^2`: `e = 2 = c`, case 2, `Θ(n^2 log n)`. `2 T(n/2) + n^2`: `e = 1 < c = 2`, case 3, `Θ(n^2)`. `T(n/2) + n`: `e = 0 < c = 1`, case 3, `Θ(n)`. The numeric table of section 2.3 shows the same growth for the last three (doubling ratios 4.19, 4.00, 2.00).

**Problem B.** `maxSeg`: `T(n) = 2 T(n/2) + 1`, `Θ(n)`. `inversions`: `T(n) = 2 T(n/2) + n`, `Θ(n log n)`. A binary search that copies the half it keeps: `T(n) = T(n/2) + n`, `Θ(n)`, not `log n`: the copy costs more than the search. A recursion calling itself twice on `n - 1`: `T(n) = 2 T(n-1) + 1`, `2^n - 1`.

**Problem C.** `{3, 1, 2}`: split into `{3}` and `{1, 2}`. The right part `{1, 2}` has no inversion. Merging `{3}` with `{1, 2}`: 1 is smaller than 3, add `mid - i = 1`; 2 is smaller than 3, add 1; total 2. `{2, 4, 1, 3, 5}`: split `{2, 4}` and `{1, 3, 5}`. The left has none; the right: `{1}` and `{3, 5}`, none. Merging: 1 is taken before 2 and 4, adding 2; then 3 is taken before 4 only, adding 1; total 3. (Both results were run.)

**Problem D.** `{1, 3, 2, 3, 1}`: the pairs are the `3` at index 1 with the `1` at index 4, and the `3` at index 3 with the same `1`: 2 pairs (`2 > 2` is false, so the `2` does not count). `{2, 4, 3, 5, 1}`: only the last element `1` can be the smaller one, and `4 > 2`, `3 > 2`, `5 > 2` are true while `2 > 2` is false: 3 pairs. Both counts were run.

**Problem E.** The left half `{-2, 1, -3, 4}` returns (sum 0, prefix 0, suffix 4, best 4); the right half `{-1, 2, 1, -5, 4}` returns (sum 1, prefix 2, suffix 4, best 4). The root returns (sum 1, prefix 2, suffix 5, best 6). The crossing term is `4 + 2 = 6`.

**Problem F.** `56 * 78`: `a1 = 5, a0 = 6, b1 = 7, b0 = 8`; `z2 = 35`, `z0 = 48`, `z1 = (11)(15) - 48 - 35 = 82`; the result is `3500 + 820 + 48 = 4368`, which is `56 * 78`.

**Problems 12.2.** All seven follow the listings. For 241, the number of results for `k` operators is the Catalan number of `k`; the memoised version makes `(k+1)(k+2)/2` range computations. For 43, the product of two numbers of up to a few hundred digits is fastest with the school method; try Karatsuba only to learn the crossover, as in section 8.

**Extra.** The speed-up depends on your machine; with the top-level merge sequential, expect clearly less than the number of threads.

---

## Cheat sheet

- Divide into independent parts, solve, combine; the combine step carries the idea.
- `T(n) = a T(n/b) + n^c`, `e = log_b a`: `c < e` gives `n^e`; `c = e` gives `n^c log n`; `c > e` gives `n^c`.
- Doubling ratio of `T`: 2 linear, a little over 2 for `n log n`, 3 for `n^1.585`, 4 for quadratic.
- Merge sort: `n log n`, depth `log n`; sorted halves make pair counting linear (inversions, 315, 493).
- Maximum subarray: return sum, prefix, suffix, best; `Θ(n)`.
- Quickselect: expected `Θ(n)`, worst `Θ(n^2)`; use a random pivot and a three-way partition.
- Parentheses (241): `3^k` calls but `(k+1)(k+2)/2` ranges: memoise.
- Closest pair: return the range sorted by y, scan a strip; `n log n`.
- Karatsuba: 3 products instead of 4, `n^1.585`; fewer operations, not always less time.
- Overlapping parts: memoise (the DP area). Unbalanced splits: depth `n`.

---

## Sources

- Titles and numbers of 53, 215 and 43 were checked against the [talentd.in NeetCode 150 sheet](https://www.talentd.in/fleetcode/sheets/neetcode-150) and, for 53 and 215, [github.com/goalong/lc](https://github.com/goalong/lc). Titles of 315, 493 and 241 were seen in search results: [halfrost, "315. Count of Smaller Numbers After Self"](https://books.halfrost.com/leetcode/ChapterFour/0300~0399/0315.Count-of-Smaller-Numbers-After-Self), [pkg.go.dev, reverse pairs](https://pkg.go.dev/github.com/awesee/leetcode/problems/reverse-pairs) and [doocs, 241](https://leetcode.doocs.org/en/lc/241/). Difficulty labels are not used because I could not verify them.
- The master theorem, the recursion-tree method, the closest-pair packing argument and Strassen's recurrence are standard results (CLRS, chapters on divide-and-conquer recurrences and on computational geometry); I stated them from memory and checked the recurrence behaviour numerically (section 2.3).
- The GMP thresholds remark in section 8 is from memory and is a general statement, not a measurement.
- All other claims in this tutorial come from the tests and measurements described at the top.
