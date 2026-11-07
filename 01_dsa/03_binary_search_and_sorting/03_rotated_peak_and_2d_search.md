# DSA 03.03: Rotated, Peak and 2D Search

*Part of the [Binary search and sorting plan](00_plan.md) · Time: about 5 hours (2 reading, 3 problems) · You need: [Binary search templates](01_binary_search_templates.md) (`firstTrue`, its invariant, and section 6 on answers that are not monotone) · Companion code: [`03_rotated_peak_and_2d_search_demo.cpp`](03_rotated_peak_and_2d_search_demo.cpp)*

By the end of this tutorial you can:

- search and find the minimum of a rotated sorted array, with and without duplicates, and say why duplicates cost O(n) in the worst case;
- find a peak in an unsorted array in O(log n), and say exactly why it works;
- search a matrix in the two ways the two matrix problems need;
- find the k-th element and the median of two sorted arrays in O(log min(m, n));
- use ternary search on a function that rises and then falls, and know when not to;
- search a sorted sequence by doubling when its size is unknown or the answer is near the front.

The companion file holds every function below and checks it against brute force on every small input (244,476 checks). It is short on purpose: the tutorial carries the examples and the problems.

---

## 1. The idea: make the question monotone

In tutorial 01 the array was sorted. In tutorial 02 the answers to a test were monotone. This tutorial searches shapes that are *not* sorted. They still have enough structure for one comparison to throw half of the range away, and each time the way to see it is the same:

**Find a yes/no question about an index `i` whose answers go no, ..., no, yes, ..., yes. Then call `firstTrue`.**

| Shape | The question about index `i` | What the first "yes" is |
|---|---|---|
| rotated sorted array | is `a[i] <= a.back()`? | the smallest element |
| array with a peak | is `a[i] > a[i + 1]`? | a peak |
| sorted pairs plus one single value | do the two entries of pair `i` differ? | the pair that holds the single value |
| two sorted arrays, cut after `t` entries of the first | is `A[t] >= B[k - t - 1]`? | the right cut for the k-th element |

Here is the function once more, since every section uses it:

```cpp
// The first value in [lo, hi) where pred is true, or hi if there is none (tutorial 01).
// pred must be false, false, ..., false, true, ..., true on the range.
template <class Index, class Pred>
Index firstTrue(Index lo, Index hi, Pred pred) {
    while (lo < hi) {                                           // invariant: pred(lo - 1) is false and pred(hi) is true, where those exist
        Index mid = lo + (hi - lo) / 2;                         // lo <= mid < hi
        if (pred(mid)) hi = mid;                                // mid may be the answer: keep it
        else lo = mid + 1;                                      // mid is false: the answer is after it
    }
    return lo;                                                  // lo == hi
}
```

---

## 2. Rotated arrays, distinct values

Take a sorted array and move its first few elements to the back: `[0, 1, 2, 4, 5, 6, 7]` becomes `[4, 5, 6, 7, 0, 1, 2]`. The array is two sorted runs; every element of the first run is larger than every element of the second. The smallest element, where the sorted order starts, is the *rotation start*.

### 2.1 Finding the rotation start

Compare each element with the **last** one. In the first run every element is larger than the last; from the rotation start on, every element is at most the last. For `[4, 5, 6, 7, 0, 1, 2]` the answers to "`a[i] <= 2`?" are no, no, no, no, **yes**, yes, yes, so the first "yes" is index 4:

```cpp
// The index of the smallest element of a rotated sorted array with distinct values: where the sorted order starts.
// Before it every element is larger than the last one, from it on every element is at most the last one.
int rotationStart(const Vec& a) {
    return firstTrue<int>(0, (int)a.size(), [&](int i) { return a[i] <= a.back(); });
}
```

An array that was not rotated at all (or rotated all the way round) gives yes everywhere, and the answer is index 0, which is right. The search:

| step | lo | hi | mid | a[mid] | a[mid] <= 2 | then |
|---|---|---|---|---|---|---|
| 1 | 0 | 7 | 3 | 7 | no | `lo = 4` |
| 2 | 4 | 7 | 5 | 1 | yes | `hi = 5` |
| 3 | 4 | 5 | 4 | 0 | yes | `hi = 4` |
| end | 4 | 4 | | | | return 4 |

Three tests for seven elements: `3:F 5:T 4:T -> 4`.

### 2.2 Searching for a value

Once you know the start, the array read from there is sorted: `b[k] = a[(start + k) % n]` is `[0, 1, 2, 4, 5, 6, 7]`. You do not have to build `b`; search it with `firstTrue` and map the index back:

```cpp
// The index of x in a rotated sorted array with distinct values, or -1. Search the sorted order
// b[k] = a[(start + k) % n] without building it.
int searchRotated(const Vec& a, int x) {
    int n = (int)a.size(), start = rotationStart(a);
    int k = firstTrue<int>(0, n, [&](int j) { return a[(start + j) % n] >= x; });
    if (k < n && a[(start + k) % n] == x) return (start + k) % n;
    return -1;
}
```

For `x = 5` the probes on `b` are `3:F 5:T 4:T -> 4`, and `(4 + 4) % 7 = 1`, so `5` is at index 1. For `x = 3` the probes are `3:T 1:F 2:F`, the first index with `b[k] >= 3` holds 4, which is not 3, so the result is -1. The cost is two searches, each at most ⌊log2 n⌋ + 1 tests.

---

## 3. Rotated arrays with duplicates

### 3.1 Why the question breaks

Take `[1, 0, 1, 1, 1]`. The smallest element is at index 1, but "`a[i] <= a.back()`?" is *yes* everywhere (every element is at most 1), so `rotationStart` answers 0. With duplicates, elements of the first run can equal the last element, and the comparison no longer separates the two runs.

### 3.2 Compare with the right end, and give up one element when equal

Compare `a[mid]` with `a[hi]` instead, and keep a closed interval `[lo, hi]`:

- `a[mid] > a[hi]`: `mid` is in the first run, because the second run never exceeds its own last element. The minimum is right of `mid`.
- `a[mid] < a[hi]`: `mid` and `hi` are in the same run. The minimum is at `mid` or before it.
- `a[mid] == a[hi]`: no information. But the value at `hi` also sits at `mid`, which is earlier, so dropping `hi` cannot lose the minimum *value*.

```cpp
// The smallest value of a rotated sorted array that may contain duplicates. Compare a[mid] with a[hi].
int minWithDuplicates(const Vec& a) {
    int lo = 0, hi = (int)a.size() - 1;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] > a[hi]) lo = mid + 1;                       // mid is in the left part, the minimum is right of it
        else if (a[mid] < a[hi]) hi = mid;                      // mid is in the right part: it may be the minimum
        else --hi;                                              // equal: a[mid] has the same value, so dropping a[hi] loses nothing
    }
    return a[lo];
}
```

On `[2, 2, 2, 0, 1]`: `lo = 0, hi = 4`, `mid = 2`, `a[2] = 2 > a[4] = 1`, so `lo = 3`; then `mid = 3`, `a[3] = 0 < a[4] = 1`, so `hi = 3`. The answer is 0 after two steps. (This is the standard argument; the demo also checks the loop on every rotation of every non-decreasing array of length up to 8 over 0 to 3.) On `[1, 0, 1, 1, 1]` the first step is the equal case (`hi = 3`), then `hi = 1`, then `lo = 1`: the answer is 0 after three steps.

The loop returns the *value*, not the rotation start. For `[0, 0, 1, 0]` it finishes at index 0, but the sorted order starts at index 3. Do not use its index to search the array.

### 3.3 The worst case is O(n)

Take `[0, 1, 1, ..., 1]` with `n` elements. Every `a[mid]` equals `a[hi]`, so each step drops one element: `n - 1` steps (999 for `n = 1000`; checked by running the loop). This is not a flaw of the loop. An algorithm that reads fewer than `n` elements of an array of ones cannot know whether the unread positions hold a 0, and `[1, 1, 0, 1, 1]` is as valid a rotation as `[0, 1, 1, 1, 1]`. So with duplicates the honest answer is "O(log n) on most inputs, O(n) in the worst case". That is the follow-up question LeetCode 81 and 154 ask.

### 3.4 Searching with duplicates

Use the closed-interval template of tutorial 01 (section 5.1). At each step, one of the halves `a[lo..mid]` and `a[mid..hi]` is sorted, and you ask whether `x` lies inside that sorted half. The test "`a[lo] <= a[mid]` means the left half is sorted" fails only when `a[lo] == a[mid] == a[hi]`: `[1, 1, 1, 0, 1]` and `[1, 0, 1, 1, 1]` look the same at those three places, but the wrap is in different halves. Then trim both ends; neither can be `x`, since each equals `a[mid]`, which was already checked.

```cpp
// Is x in a rotated sorted array that may contain duplicates? Closed interval. One half of [lo, hi] is sorted, unless
// a[lo] == a[mid] == a[hi]: then nothing can be said and both ends are trimmed.
bool containsRotatedDup(const Vec& a, int x) {
    int lo = 0, hi = (int)a.size() - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] == x) return true;
        if (a[lo] == a[mid] && a[mid] == a[hi]) { ++lo; --hi; }
        else if (a[lo] <= a[mid]) {                             // a[lo..mid] is sorted
            if (a[lo] <= x && x < a[mid]) hi = mid - 1;
            else lo = mid + 1;
        } else {                                                // a[mid..hi] is sorted
            if (a[mid] < x && x <= a[hi]) lo = mid + 1;
            else hi = mid - 1;
        }
    }
    return false;
}
```

For `[1, 0, 1, 1, 1]` and `x = 0`: the first step trims (`lo = 1, hi = 3`); the second sees the left half `[0, 1]` sorted and `0` inside it (`hi = 1`); the third finds `a[1] == 0`. Three steps.

---

## 4. Peaks, and pairs

### 4.1 A peak in an unsorted array

A *peak* is an element larger than both neighbours; outside the array counts as minus infinity, so an end element can be a peak. In `[1, 2, 1, 3, 5, 6, 4]` the peaks are at index 1 (value 2) and index 5 (value 6). We want any one of them, and we promise that neighbouring elements differ.

The array is not sorted, but ask "`a[i] > a[i + 1]`?", the question "is the array going down after `i`?". Tutorial 01 showed that `firstTrue` returns *a* boundary even when the answers are a jumble: at the result `r`, the answer at `r - 1` is no and the answer at `r` is yes. Here that reads:

- the answer at `r - 1` is no: `a[r - 1]` is not larger than `a[r]`, and neighbours differ, so `a[r - 1] < a[r]`;
- the answer at `r` is yes: `a[r] > a[r + 1]`.

So `a[r - 1] < a[r] > a[r + 1]`: a peak. If `r` is 0, there is no left neighbour; if every answer is no, `r` is the last index and `a[n - 2] < a[n - 1]`, which also makes it a peak. Ask the question only for `i < n - 1`:

```cpp
// The index of a peak: an element larger than both neighbours, where outside the array counts as minus infinity.
// Neighbouring elements differ. a[i] > a[i + 1] is only asked for i < n - 1; if every answer is no, a[n - 1] is the peak.
int findPeak(const Vec& a) {
    return firstTrue<int>(0, (int)a.size() - 1, [&](int i) { return a[i] > a[i + 1]; });
}
```

On `[1, 2, 1, 3, 5, 6, 4]` the probes are `3:F 5:T 4:F -> 5`: index 3 is going up (`3 < 5`), so go right; index 5 is going down (`6 > 4`), so keep it; index 4 is going up, so the answer is 5. On `[1, 2, 3]` the single probe `1:F` ends at 2, the last index. Index 1 is also a peak in the first array; which peak you get depends on the probes.

The same code solves the *mountain array* (LeetCode 852), where the values go up to one peak and then down, and the peak is unique.

### 4.2 Flat spots break it

If neighbours may be equal, the question stops being informative. On `[1, 1, 1, 1, 5, 1, 1, 1]` the probes `3:F 5:F 6:F` end at index 7, which is not a peak (`1 > 1` is false), while the real peak is at index 4. The 5 can hide among equal values, and by the same argument as in 3.3 no algorithm can find it with fewer than `n` reads. Everything in this section needs strict steps.

### 4.3 The single element in a sorted array of pairs

A sorted array where every value appears twice, except one that appears once: `[1, 1, 2, 3, 3, 4, 4, 8, 8]`. Group the entries in pairs `(a[0], a[1]), (a[2], a[3]), ...`. Before the single value, each pair holds two equal numbers. From the single value on, everything is shifted by one, so each pair holds the second copy of one value and the first copy of the next, and the two differ. "Does pair `q` hold two different numbers?" goes no, ..., no, yes, ..., yes.

```cpp
// The value that appears once in a sorted array (odd length) where every other value appears twice. Pair q is
// a[2q], a[2q + 1]: before the single value every pair matches, from its position on no pair does. If every pair
// matches, the single value is the last element, which firstTrue reports as "none" (q = n / 2).
int singleElement(const Vec& a) {
    int q = firstTrue<int>(0, (int)a.size() / 2, [&](int p) { return a[2 * p] != a[2 * p + 1]; });
    return a[2 * q];
}
```

The length is odd, so there are `n / 2` complete pairs; if all of them match, the single value is the last element, which is the "none" result of `firstTrue`. On the example the probes are `2:T 1:T 0:F -> 1`, and `a[2 * 1] = 2` is the single value.

---

## 5. Matrices

### 5.1 Rows in order, and each row starting above the last (LeetCode 74)

If each row is sorted and each row starts above the end of the row before it, reading the matrix by rows gives one sorted array of `rows * cols` entries. Index `j` is row `j / cols`, column `j % cols`:

```cpp
// Each row sorted and the first entry of a row above the last entry of the row before: the matrix is one sorted array
// of rows * cols entries, read by row.
bool containsFlat(const Matrix& m, int x) {
    int rows = (int)m.size(), cols = (int)m[0].size();
    int k = firstTrue<int>(0, rows * cols, [&](int j) { return m[j / cols][j % cols] >= x; });
    return k < rows * cols && m[k / cols][k % cols] == x;
}
```

For `[[1, 3, 5, 7], [10, 11, 16, 20], [23, 30, 34, 60]]` and `x = 3`: 12 entries, probes `6:T 3:T 1:T 0:F -> 1`, which is row 0, column 1. Four tests, ⌊log2 12⌋ + 1.

### 5.2 Rows sorted and columns sorted (LeetCode 240)

Here the rows and columns are each sorted, but nothing relates the end of one row to the start of the next: in `[[1, 4], [2, 5]]` reading by rows gives 1, 4, 2, 5, which is not sorted. Do not flatten it. Start at the **top-right corner**, which is the largest in its row and the smallest in its column:

- the entry is larger than `x`: everything below it in its column is larger too, so drop the column (`--c`);
- the entry is smaller than `x`: everything to its left in its row is smaller too, so drop the row (`++r`).

```cpp
// Rows sorted and columns sorted. Start at the top-right corner. An entry larger than x: everything below it is larger
// too, so drop its column. An entry smaller than x: everything left of it is smaller, so drop its row.
bool containsStaircase(const Matrix& m, int x) {
    int r = 0, c = (int)m[0].size() - 1;
    while (r < (int)m.size() && c >= 0) {
        if (m[r][c] == x) return true;
        if (m[r][c] > x) --c;
        else ++r;
    }
    return false;
}
```

Each step removes a row or a column, so there are at most `rows + cols - 1` steps. On

```
 1  4  7 11 15
 2  5  8 12 19
 3  6  9 16 22
10 13 14 17 24
18 21 23 26 30
```

`x = 5` takes 5 steps: 15, 11 and 7 are too big (left), 4 is too small (down), 5 is found. `x = 20` is not there and takes the worst case, 9 steps: 15 (down), 19 (down), 22 (left), 16 (down), 17 (down), 26 (left), 23 (left), 21 (left), 18 (down, off the bottom).

### 5.3 Staircase or a search in every row?

You can also binary search each row: `rows × (⌊log2 cols⌋ + 1)` tests. Which is smaller depends on the shape:

| Matrix | staircase, at most `rows + cols - 1` | a search in every row |
|---|---|---|
| 5 × 5 | 9 | 15 |
| 300 × 300 | 599 | 2700 |
| 10 × 1 000 000 | 1 000 009 | 200 |

For squarish matrices the staircase wins; for a few very long rows, search each row. (LeetCode 240 allows 300 × 300 at most.)

---

## 6. Two sorted arrays: the k-th element and the median

Given sorted `A` (length `m`) and `B` (length `n`), find the k-th smallest of all `m + n` entries without merging (LeetCode 4 asks for the median in O(log(m + n))).

### 6.1 The idea: cut both arrays

Take `t` entries from the front of `A` and `k - t` from the front of `B`. These `k` entries are the `k` smallest overall exactly when nothing taken is larger than anything left. Each array is sorted, so only two comparisons across the cuts matter:

`A[t - 1] <= B[k - t]` and `B[k - t - 1] <= A[t]`.

Example: `A = [1, 3, 8, 9, 15]`, `B = [7, 11, 18, 19, 21, 25]`, 11 entries, so the median is the 6th smallest. Take `t = 4` from `A` and 2 from `B`:

```
taken:  A: 1 3 8 9      B: 7 11
left:   A: 15           B: 18 19 21 25
```

`9 <= 18` and `11 <= 15`, so the 6 smallest are `1, 3, 7, 8, 9, 11` and the 6th is `max(9, 11) = 11`.

### 6.2 Finding the cut with firstTrue

Move `t` to the right: `A[t]` grows and `B[k - t - 1]` shrinks. So "`A[t] >= B[k - t - 1]`?" ("have we taken enough from `A`?") goes no, ..., no, yes, ..., yes. At the first "yes" `t`, the previous answer was no, which says `A[t - 1] < B[k - t]`, and the yes says the other condition: both conditions hold. Which `t` are possible? Not more than `m` or `k`, and not fewer than `k - n` (we cannot take more than `n` from `B`): `t` is in `[max(0, k - n), min(k, m)]`. At `t = min(k, m)` the answer is always yes (either `A[m]` is past the end, or `k - t - 1 = -1` is before the start of `B`), so the range for `firstTrue` can stop there and "none" means `t = hi`. Outside the arrays count as minus infinity on the left and plus infinity on the right:

```cpp
// The k-th smallest (k from 1) of two sorted arrays. Take t elements from a and k - t from b. The split is right when
// a[t - 1] <= b[k - t] and b[k - t - 1] <= a[t]. As t grows, a[t] grows and b[k - t - 1] shrinks, so
// "a[t] >= b[k - t - 1]" goes false, ..., false, true, ..., true. Outside the arrays: -infinity left, +infinity right.
int kthOfTwo(const Vec& a, const Vec& b, int k) {
    int m = (int)a.size(), n = (int)b.size();
    auto A = [&](int i) { return i < 0 ? INT_MIN : i >= m ? INT_MAX : a[i]; };
    auto B = [&](int j) { return j < 0 ? INT_MIN : j >= n ? INT_MAX : b[j]; };
    int lo = std::max(0, k - n), hi = std::min(k, m);           // t is in [lo, hi], and the test is true at hi
    int t = firstTrue<int>(lo, hi, [&](int s) { return A(s) >= B(k - s - 1); });   // no smaller t works: t = hi
    return std::max(A(t - 1), B(k - t - 1));
}
```

On the example, `k = 6`, `t` in `[0, 5]`:

| step | lo | hi | t = mid | A[t] | B[k - t - 1] | A[t] >= B[k - t - 1] | then |
|---|---|---|---|---|---|---|---|
| 1 | 0 | 5 | 2 | 8 | 19 | no | `lo = 3` |
| 2 | 3 | 5 | 4 | 15 | 11 | yes | `hi = 4` |
| 3 | 3 | 4 | 3 | 9 | 18 | no | `lo = 4` |
| end | 4 | 4 | | | | | return 4 |

### 6.3 The median

If the total is odd, the median is the `(total + 1) / 2`-th smallest. If it is even, it is the mean of the `total / 2`-th and the next one:

```cpp
// The median of two sorted arrays (not both empty): the middle value, or the mean of the two middle values.
double medianOfTwo(const Vec& a, const Vec& b) {
    int total = (int)(a.size() + b.size());
    int low = kthOfTwo(a, b, (total + 1) / 2);
    if (total % 2 == 1) return low;
    return (low + (double)kthOfTwo(a, b, total / 2 + 1)) / 2;
}
```

Even example: `A = [2, 4, 6]`, `B = [1, 3, 5, 7, 9]`. The merged order is `1 2 3 4 5 6 7 9`, so the median is (4 + 5) / 2 = 4.5. For the 4th smallest the probes are `1:F 2:T -> 2` (two from each array: `2 4` and `1 3`, and the larger of `4` and `3` is 4).

### 6.4 The cost

For the median, the range of `t` has exactly `min(m, n)` positions, so each search makes at most ⌊log2 min(m, n)⌋ + 1 tests (10 for the 1000-element arrays of LeetCode 4; the function runs it twice for an even total, still logarithmic). The usual advice, "binary search the shorter array", is built in. Empty arrays work: if `A` is empty the range is `[0, 0]`, one position wide, and the answer is `B[k - 1]`.

---

## 7. Unimodal functions: ternary search

A function is **unimodal** if it strictly rises and then strictly falls (or the mirror image). Finding its top is the peak problem again.

**On integers, use the peak search.** Ask "`f(i) < f(i + 1)`?" (still rising), which is `findPeak` with `f` in place of the array. For `n = 10^6` that is 20 rounds of two evaluations, 40 evaluations.

**On real numbers there is no "next index", so compare two points.** *Ternary search* looks at the two thirds of the interval. If `f(m1) < f(m2)`, the top is right of `m1`: drop the left third. Otherwise it is left of `m2`: drop the right third. Every round keeps 2/3 of the width:

```cpp
// The point in [lo, hi] where f is largest, for an f that strictly rises and then strictly falls. Each round compares
// f at the two thirds and drops the outer third that cannot hold the top.
template <class F>
double ternaryMax(double lo, double hi, int rounds, F f) {
    for (int i = 0; i < rounds; ++i) {
        double m1 = lo + (hi - lo) / 3, m2 = hi - (hi - lo) / 3;
        if (f(m1) < f(m2)) lo = m1;                             // the top is right of m1
        else hi = m2;                                           // the top is left of m2
    }
    return lo + (hi - lo) / 2;
}
```

To shrink a width of 20 to 10^-9 takes ⌈log(20 / 10^-9) / log 1.5⌉ = 59 rounds, 118 evaluations (the demo reaches an error of 1.6 × 10^-10 on `-(x - 0.3)^2`). On integers the same method would need 35 rounds, 70 evaluations, against the 40 above.

Two traps:

- **A flat top hides the difference.** Near the top `f` is flat, and two values closer than one unit in the last place compare equal. For `f(x) = 1000 - (x - 0.3)^2` that unit is 1.1 × 10^-13, so points closer than about √(1.1 × 10^-13) = 3.4 × 10^-7 cannot be told apart: after 200 rounds the demo's answer is off by 2.4 × 10^-7, not 10^-9 (measured with g++ and clang++ on x86-64). Do not ask for more precision than the function values can show.
- **Flat stretches elsewhere** break it as in 4.2: if the function can be equal at two points on the same side of the top, the comparison says nothing. The rise and the fall must be strict.

---

## 8. Exponential search

Binary search needs to know `n` and costs ⌊log2 n⌋ + 1 reads wherever the answer is. When the size is unknown (a stream, a file you cannot size, an array that ends at an unknown point) or the answer is probably near the front, **double first, then search the last step**, as in tutorial 02, section 9.3. The answer `p` (the first index with `a[p] >= x`) is bracketed in `(bound / 2, bound]`:

```cpp
// The first index in [0, n] whose element is >= x, for a sorted sequence read through get(i). Doubling brackets the
// answer p in (bound / 2, bound] with about log2(p) reads and a binary search inside needs about log2(p) more: the cost
// follows the position of the answer, not n. (Use 64-bit indexes when n is near 2^31.)
template <class Get>
int gallopLowerBound(int n, Get get, int x) {
    if (n == 0 || get(0) >= x) return 0;
    int bound = 1;                                              // get(bound / 2) < x is known (get(0) when bound = 1)
    while (bound < n && get(bound) < x) bound *= 2;
    return firstTrue<int>(bound / 2 + 1, std::min(bound, n), [&](int i) { return get(i) >= x; });
}
```

`get(i)` reads element `i`. If the size is unknown, let `get(i)` return plus infinity past the end and replace `bound < n` by `true`. The demo counts reads on `a[i] = i`:

| answer at `p` | reads, n = 10^6 | reads, n = 10^9 |
|---|---|---|
| 10 | 9 | 9 |
| 1000 | 21 | 21 |
| 100 000 | 35 | 35 |
| plain binary search, worst case | 20 | 30 |

It costs about 2 × log2 of the position, so it wins only when the answer is within roughly the first √n entries (at `n = 10^6` the position 1000 already loses, 21 against 20).

---

## 9. Where this shows up

- **A circular log.** A ring buffer of records with increasing timestamps is a rotated sorted array: the oldest record sits right after the write pointer. If the pointer is lost, `rotationStart` on the timestamps finds it in O(log n) reads, *as long as timestamps are distinct and increasing* (a fit to the problem, not a claim about any particular product).
- **Time-based lookup.** LeetCode 981 asks for the value stored at the latest timestamp not above `t`. Timestamps are added in increasing order, so each key's list is sorted: it is `upper_bound(t) - 1` from tutorial 01.
- **Sorting.** Python's sort (TimSort) merges sorted runs. When one run keeps supplying the next elements, it switches to *galloping mode*, which uses an exponential search to bracket where the next element of the other run belongs and then a binary search inside the bracket (Wikipedia, Timsort).

---

## 10. Mistakes that cost points

1. **Comparing with `a[lo]` instead of the end** when looking for the rotation start. It fails even on the textbook example (Problem B1).
2. **`>=` where the duplicate case needs three branches** (Problem B2): `a[mid] == a[hi]` must shrink `hi` by one, not move `lo`.
3. **Promising O(log n) with duplicates.** Say "O(n) in the worst case, and here is the array that forces it".
4. **Asking `a[i] > a[i + 1]` for `i = n - 1`.** Search `[0, n - 1)` (Problem B3).
5. **Peak search or ternary search on flat stretches.** Check that neighbours differ, or that the steps are strict.
6. **Flattening the wrong matrix.** Flat search is for rows that continue each other (74). For 240 use the staircase from the top-right (or bottom-left) corner; from the top-left both moves go up and nothing is decided.
7. **Swapped row and column** in the flat index: row `j / cols`, column `j % cols`.
8. **Forgetting the range of `t`** in the two-array search, or the sentinels, and indexing past an empty array.
9. **Using the index from `minWithDuplicates`** as the rotation start (section 3.2).

---

## 11. Questions you will be asked

**How do you search a rotated sorted array?** Find the rotation start by comparing each element with the last one (distinct values), then binary search the sorted order, or decide at each step which half is sorted. Two O(log n) searches.

**What changes with duplicates, and why?** The comparison with the last element stops separating the runs. When `a[mid] == a[hi]` you can only drop one element, so the worst case is O(n), and no algorithm can do better because the minimum can hide among equal values.

**Why does binary search find a peak in an array that is not sorted?** At the result of `firstTrue` the answer one step before is no and the answer there is yes: it is larger than its left neighbour and than its right neighbour. It needs neighbours that differ.

**Median of two sorted arrays in O(log min(m, n))?** Cut both arrays so that the left parts hold half of the entries; the cut is right when the largest left entries are not above the smallest right entries; searching the cut in the shorter array is a monotone yes/no test.

**Rows sorted and columns sorted: why start in a corner?** From the top-right corner each comparison removes a whole row or a whole column, so the search takes at most `rows + cols - 1` steps.

**When would you use exponential search?** When the size is unknown, or the target is likely near the front: the cost grows with the position of the answer, about 2 × log2 of it, not with `n`.

---

## 12. Problems

**How to use this section.** Try each problem for 15 minutes (easy) or 30 minutes (harder) before reading anything. If you are stuck, read only the **How** lines, then try again, and only then open the answer at the bottom. Log each attempt in the [practice log](../practice_log.md).

### On paper

**Problem A: Trace.** The array `[15, 18, 2, 3, 6, 12]` is rotated. Write the answers to "`a[i] <= a.back()`?" for every index, the probes of `rotationStart` over `[0, 6)`, and then the probes of `searchRotated` for `x = 3`.
*Where:* paper. *How:* the tables of 2.1 and 2.2; the sorted order starts at the rotation start.

**Problem B: Find the bug.** Each function should do what its comment says. For each, give an input that breaks it and say what happens.

```cpp
// 1. the smallest element of a rotated array with distinct values
int f1(const Vec& a) {
    int lo = 0, hi = (int)a.size() - 1;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] > a[lo]) lo = mid + 1;
        else hi = mid;
    }
    return a[lo];
}

// 2. the smallest element of a rotated array that may contain duplicates
int f2(const Vec& a) {
    int lo = 0, hi = (int)a.size() - 1;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] >= a[hi]) lo = mid + 1;
        else hi = mid;
    }
    return a[lo];
}

// 3. the index of a peak (neighbouring elements differ)
int f3(const Vec& a) {
    return firstTrue<int>(0, (int)a.size(), [&](int i) { return a[i] > a[i + 1]; });
}
```
*Where:* paper. *How:* try each on a tiny input; compare with sections 2.1, 3.2 and 4.1.

**Problem C: Can binary search help?** For each, say yes or no, and the best cost you can promise: (1) the minimum of a rotated sorted array with many duplicates; (2) the maximum of an array that strictly rises and then strictly falls; (3) a value in a matrix whose rows are sorted but whose columns are not; (4) a peak in an unsorted array whose neighbours differ; (5) the value that appears once in an *unsorted* array where every other value appears twice.
*Where:* paper. *How:* ask what a single comparison lets you throw away, and whether a hidden value could defeat you.

**Problem D: Cut by hand.** For `A = [2, 4, 6]` and `B = [1, 3, 5, 7, 9]` find the 5th smallest: give the range of `t`, the probes, the result and the two comparisons that confirm the cut.
*Where:* paper. *How:* section 6.2; the range is `[max(0, k - n), min(k, m)]`.

**Problem E: Which matrix search?** For a 300 × 300 matrix and for a 10 × 10^6 matrix with sorted rows and columns, give the number of steps of the staircase and of a search in every row, and choose.
*Where:* paper. *How:* the table of 5.3.

### In code

**Problem 1: Find Minimum in Rotated Sorted Array and Search in Rotated Sorted Array** (LeetCode 153 and 33, medium)
*Where:* leetcode.com/problems/find-minimum-in-rotated-sorted-array and leetcode.com/problems/search-in-rotated-sorted-array. *Goal:* the minimum, then the position of a target, in O(log n). Then compare both with brute force on every rotation of every subset of 0 to 9.
*How:* section 2. Compare with the last element, not the first.

**Problem 2: Find Minimum in Rotated Sorted Array II and Search in Rotated Sorted Array II** (LeetCode 154, hard, and 81, medium)
*Where:* leetcode.com/problems/find-minimum-in-rotated-sorted-array-ii and leetcode.com/problems/search-in-rotated-sorted-array-ii. *Goal:* the same with duplicates. Build the input that forces `n - 1` steps and count the steps.
*How:* section 3. Three branches; trim both ends in the search.

**Problem 3: Find Peak Element** (LeetCode 162, medium) and **Peak Index in a Mountain Array** (LeetCode 852, medium)
*Where:* leetcode.com/problems/find-peak-element and leetcode.com/problems/peak-index-in-a-mountain-array. *Goal:* any peak in O(log n).
*How:* section 4.1. Search `[0, n - 1)`.

**Problem 4: Single Element in a Sorted Array** (LeetCode 540, medium)
*Where:* leetcode.com/problems/single-element-in-a-sorted-array. *Goal:* O(log n) time and O(1) space.
*How:* section 4.3. Search over pairs, not over elements.

**Problem 5: Search a 2D Matrix and Search a 2D Matrix II** (LeetCode 74 and 240)
*Where:* leetcode.com/problems/search-a-2d-matrix and leetcode.com/problems/search-a-2d-matrix-ii. *Goal:* one flat search and one staircase.
*How:* sections 5.1 and 5.2. They look the same and are not.

**Problem 6: Median of Two Sorted Arrays** (LeetCode 4, hard)
*Where:* leetcode.com/problems/median-of-two-sorted-arrays. *Goal:* O(log min(m, n)). Then compare with a merge on random arrays, including empty ones and equal values.
*How:* section 6. Write `kthOfTwo` first.

**Problem 7: Time Based Key-Value Store** (LeetCode 981)
*Where:* leetcode.com/problems/time-based-key-value-store. *Goal:* `set` and `get` where `get` returns the value at the latest timestamp not above the query.
*How:* a vector of `(timestamp, value)` per key; `upper_bound` minus one.

**Problem 8: Gallop** (your own editor)
*Where:* your editor. *Goal:* write `gallopLowerBound` with a `get(i)` that counts reads, and compare the reads with `std::lower_bound` for answers at 10, 1000 and 10^5 in an array of 10^6 entries.
*How:* section 8.

**More practice:** Find in Mountain Array (1095, hard, interactive: at most 100 `get` calls), H-Index II (275, medium), Find a Peak Element II (1901, medium, O(m log n)).

---

## Answers

### Problem A

The answers are no, no, yes, yes, yes, yes (15 and 18 are above 12). The probes over `[0, 6)` are `3:T 1:F 2:T -> 2`, so the rotation start is index 2 (the value 2). The sorted order is `2 3 6 12 15 18`. For `x = 3` the probes on it are `3:T 1:T 0:F -> 1`, so the position is `(2 + 1) % 6 = 3`.

### Problem B

1. `f1` compares with `a[lo]`, which moves. On `[4, 5, 6, 7, 0, 1, 2]` it returns **2** instead of 0: after the first step `lo = 4` is inside the second run, where every later entry exceeds `a[lo] = 0`, so it keeps moving right past the minimum. Compare with `a[hi]`.
2. `f2` treats `a[mid] == a[hi]` as "go right". On `[3, 1, 3, 3, 3]` it returns **3** instead of 1: `mid = 2` equals `a[4]`, so `lo = 3` and the 1 is skipped. The equal case must do `--hi`.
3. `f3` searches `[0, n)`, so it can ask `a[i + 1]` for `i = n - 1`. On `[1, 2, 3]` the probes are `1:F`, then `2`, which reads `a[3]`: out of bounds. Search `[0, n - 1)`.

### Problem C

1. No: at best O(n) in the worst case (3.3). 2. Yes, O(log n): `f(i) < f(i + 1)`. 3. No single search: one search per row. 4. Yes, O(log n) (4.1). 5. No: without order a comparison tells you nothing about where the single value is; use XOR or a hash set, O(n).

### Problem D

`k = 5`, `m = 3`, `n = 5`: `t` is in `[max(0, 0), min(5, 3)] = [0, 3]`. The probes are `1:F 2:T -> 2`: at `t = 1` we have `A[1] = 4 < B[3] = 7`, at `t = 2` we have `A[2] = 6 >= B[2] = 5`. Two from `A` (`2 4`) and three from `B` (`1 3 5`): `max(4, 5) = 5`. Check: `4 <= B[3] = 7` and `5 <= A[2] = 6`.

### Problem E

300 × 300: staircase at most 599, rows 300 × 9 = 2700: staircase. 10 × 10^6: staircase up to 1 000 009, rows 10 × 20 = 200: search each row.

### Problems 1 to 8

1. `rotationStart` with `firstTrue(0, n, i => a[i] <= a.back())`; then `firstTrue(0, n, k => a[(start + k) % n] >= x)` and a check of the found entry.
2. The three-branch loop for the minimum; the closed-interval loop with the trim for the search. The forcing input is `[0, 1, 1, ..., 1]`.
3. `firstTrue(0, n - 1, i => a[i] > a[i + 1])`. For the mountain array the result is the unique peak.
4. `firstTrue(0, n / 2, q => a[2q] != a[2q + 1])`, answer `a[2 * result]`.
5. 74: `firstTrue(0, rows * cols, j => m[j / cols][j % cols] >= x)` and check the entry. 240: start at `(0, cols - 1)`, `--c` when larger, `++r` when smaller.
6. `kthOfTwo` as in 6.2, then the median as in 6.3. Test against a sort of the merged arrays.
7. `get`: `upper_bound` on the key's timestamps, minus one; empty string if the result is the first position.
8. As in section 8; with `get` counting reads, the numbers in the table are what you should see.

---

## Cheat sheet

- **Idea:** find a yes/no question on an index that goes no, ..., yes, ..., yes, then `firstTrue`.
- **Rotated, distinct:** question `a[i] <= a.back()`; first yes = minimum; search the sorted order `a[(start + k) % n]`.
- **Rotated, duplicates:** compare `a[mid]` with `a[hi]`; equal: `--hi`; worst case O(n). Search: pick the sorted half; if the three ends are equal, trim both.
- **Peak:** question `a[i] > a[i + 1]` over `[0, n - 1)`; neighbours must differ.
- **Pairs plus one single:** question "pair `q` mismatches" over `[0, n / 2)`.
- **Matrix:** rows continue each other: flat index. Rows and columns sorted: staircase from a corner, at most `rows + cols - 1` steps.
- **Two sorted arrays:** cut `t` from `A`, `k - t` from `B`; question `A[t] >= B[k - t - 1]` over `[max(0, k - n), min(k, m)]`; the k-th is `max(A[t - 1], B[k - t - 1])`.
- **Unimodal:** integers: the peak search on `f(i) < f(i + 1)`. Reals: ternary search, strict steps, precision limited by the function values.
- **Unknown size or answer near the front:** double, then search the last step; about 2 × log2 of the position.

**Sources** (checked while writing this tutorial):
- LeetCode problem statements for 4, 81, 153, 154, 162, 540 and 852 (constraints, examples, difficulty), read from the [doocs.org mirror](https://leetcode.doocs.org/en/lc/154) of the statements; 33 from [a statement mirror](https://ycj.codeberg.page/problems/0033-search-in-rotated-sorted-array); 74 from [dyclassroom](https://dyclassroom.com/leetcode-problems/74-search-a-2d-matrix) and 240 from [designgurus](https://www.designgurus.io/answers/detail/240-search-a-2d-matrix-ii-2search4) (statements and constraints only; their difficulty was not stated, so it is not given here); 981 from [a statement copy](https://cnblogs.com/Dylan-Java-NYC/p/11921783.html). More practice: 1095 and 275 from the [doocs.org mirror](https://leetcode.doocs.org/en/lc/1095/) ([275](https://leetcode.doocs.org/en/lc/275/)), 1901 from [algo.monster](https://algo.monster/liteproblems/1901).
- TimSort and galloping mode: [Wikipedia's Timsort article](https://wiki.krisyotam.com/content/wikipedia_en_all_nopic_2026-03/Timsort) (read through a mirror).

**Next:** tutorial 04, sorting algorithms inside (merge sort, quicksort and its partitions, heapsort, counting, bucket and radix sort, the n log n lower bound, stability, introsort). Say "next" when you are ready.
