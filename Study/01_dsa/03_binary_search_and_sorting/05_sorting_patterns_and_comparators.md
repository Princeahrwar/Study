# DSA 03.05: Sorting Patterns and Comparators

*Part of the [Binary search and sorting plan](00_plan.md) · Time: about 6 hours (2.5 reading, 3.5 problems) · You need: [Sorting algorithms inside](04_sorting_algorithms_inside.md) (partitions, heaps, stability) · Companion code: [`05_sorting_patterns_and_comparators_demo.cpp`](05_sorting_patterns_and_comparators_demo.cpp)*

By the end of this tutorial you can:

- say what a comparator must promise (a strict weak ordering), test one, and name what breaks when it lies;
- write multi-key and custom-rule comparators, and sort indexes instead of values;
- find the k-th element in expected O(n) with quickselect and in worst-case O(n) with median of medians, and say what `std::nth_element` does;
- choose between a full sort, a heap and a selection for the top K;
- sort intervals by the right key, merge them and count the rooms they need.

The companion file (C++20) holds every function below and checks each against brute force on every small input (635,950 checks). It is short on purpose: the tutorial carries the examples and the problems.

---

## 1. What a comparator promises

### 1.1 The contract

`std::sort`, `std::set`, `std::map` and `std::priority_queue` take a function `comp(a, b)` that means "a goes before b". Everything they do assumes `comp` is a **strict weak ordering**:

| Rule | In words | Broken by |
|---|---|---|
| Irreflexive | `comp(a, a)` is false | `a <= b` |
| Asymmetric | `comp(a, b)` and `comp(b, a)` are never both true | `a - b` used as a bool |
| Transitive | `comp(a, b)` and `comp(b, c)` give `comp(a, c)` | rock, paper, scissors |
| Equivalence is transitive | if neither of `a`, `b` goes first, and neither of `b`, `c`, then neither of `a`, `c` | `a + 3 < b`; `<` on doubles with NaN |

*Equivalent* means "neither goes first": equal as far as this sort cares. Comparing by `a % 10` makes 3, 13 and 23 equivalent, and that is fine. A simple way to be safe: **compare keys with `<`**. If `comp(a, b)` is `key(a) < key(b)` for numbers, strings or tuples, all four rules hold by construction.

### 1.2 Several keys

The classic bug is `a.x < b.x || a.y < b.y`: it is true in both directions for `(0, 1)` and `(1, 0)`. The second key counts only when the first one ties:

```cpp
struct Student {
    std::string name;
    int grade;
};

// Highest grade first; equal grades by name. Compare the first key, and only when it ties the next one.
bool byGradeThenName(const Student& a, const Student& b) {
    if (a.grade != b.grade) return a.grade > b.grade;
    return a.name < b.name;
}

// The same order with std::tie: lists compare key by key. Descending order = swap the two sides of that key.
bool byGradeThenNameTie(const Student& a, const Student& b) {
    return std::tie(b.grade, a.name) < std::tie(a.grade, b.name);
}
```

Sorting `Cy 80, Ann 90, Bob 80, Di 90, Ed 70` this way gives `Ann Di Bob Cy Ed`. The `std::tie` version needs no `if`: tuples compare key by key, and a **descending** key is written by swapping the two sides of that key (`b.grade` on the left).

### 1.3 What happens when the contract is broken

Measured with the library of GCC 13.3 on this machine, with `std::sort(v.begin(), v.end(), [](int a, int b) { return a <= b; })`. This is undefined behaviour, so another library or machine may do something else:

- 16 equal ints: returns normally (at this size the sort is only an insertion sort).
- 17 equal ints: the sort reads outside the vector (AddressSanitizer reports a `heap-buffer-overflow` in the partition code). Without the sanitizer the call returns, and `v[0]` is 0, a number that was never in the vector (g++ at `-O0`, `-O1` and `-O2`, and clang++ with the same library).
- 1000 values of `std::mt19937(1)() % 5`: the program crashed in every run I made, usually with an abort and a glibc heap-corruption message (`malloc(): invalid size (unsorted)` or `invalid next size`), now and then with a segmentation fault. With `a >= b` instead it ended in a segmentation fault every time (g++ `-O0` and `-O2`).
- With `-D_GLIBCXX_DEBUG`, the first call aborts at once with "comparison doesn't meet irreflexive requirements".

The reason is in tutorial 04 (section 9.2): the partition loops of `std::sort` have no bounds checks. They rely on an element that is "not less than the pivot" to stop them, and with `<=` even the pivot itself is "less than the pivot", so the scan walks off the end.

A broken comparator does not always crash; sometimes it just sorts wrongly. With `<` on doubles and one NaN, 3 of the 24 orders of `{1, 2, 3, NaN}` come back with the numbers out of order (`1 3 nan 2` is returned unchanged), because NaN is "equivalent" to every number while 2 and 3 are not equivalent to each other. Java's `TimSort` may throw "Comparison method violates its general contract!", but only on some inputs (JDK-8134576). The fix for NaN is to put them last on purpose: `std::isnan(b) ? !std::isnan(a) : a < b`.

So when a sort crashes or is wrong "only sometimes", suspect the comparator first, and build your tests with `-D_GLIBCXX_DEBUG`.

### 1.4 Testing a comparator

For a small sample of values, check the four rules on every pair and triple:

```cpp
// Does `less` behave as a strict weak ordering on these sample values? Tries every pair and every triple (for tests only:
// O(n^3)). The four rules: never less than itself; never both a < b and b < a; a < b < c gives a < c; "neither is less"
// (equivalent) is transitive.
template <class T, class Less>
bool isStrictWeakOrder(const std::vector<T>& sample, Less less) {
    auto equiv = [&](const T& a, const T& b) { return !less(a, b) && !less(b, a); };
    for (const T& a : sample) {
        if (less(a, a)) return false;
        for (const T& b : sample) {
            if (less(a, b) && less(b, a)) return false;
            for (const T& c : sample) {
                if (less(a, b) && less(b, c) && !less(a, c)) return false;
                if (equiv(a, b) && equiv(b, c) && !equiv(a, c)) return false;
            }
        }
    }
    return true;
}
```

This is for tests only (O(n³)). The companion file runs it on these, with the sample 0 to 29 for the numbers:

| Comparator | Strict weak ordering? | Why |
|---|---|---|
| `a < b` and `a % 10 < b % 10` | yes | both compare keys |
| `a <= b` | no | `a <= a` is true |
| `a.x < b.x \|\| a.y < b.y` | no | both directions can be true |
| `a + 3 < b` ("less by more than 3") | no | 0, 3 and 6: 0 is equivalent to 3 and 3 to 6, but 0 is less than 6 |
| `(bool)(a - b)` | no | true whenever `a != b`, in both directions |
| `a < b` on doubles with NaN | no | NaN is equivalent to everything |
| NaN last, then `a < b` | yes | the NaNs form one class after all numbers |

---

## 2. Writing comparators

### 2.1 Sort the indexes

Often you need the order, not the sorted values: to keep the original positions, or to reorder several parallel arrays by one of them. Sort the indexes and compare the values they point to:

```cpp
// The indexes of v in sorted order (v itself is not changed). Stable: equal values keep the smaller index first.
std::vector<int> argsort(const std::vector<int>& v) {
    std::vector<int> idx(v.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(), [&](int i, int j) { return v[i] < v[j]; });
    return idx;
}
```

For `[30, 10, 20, 10]` the answer is `1 3 2 0`: the two 10s keep their order because `stable_sort` is used (tutorial 04, section 8).

### 2.2 Expensive keys

A comparator that computes a key for both of its arguments computes it twice per comparison. Merge sort made 8,699 comparisons on 1000 values (tutorial 04, section 6.2): 17,398 key computations for 1000 keys. If the key is costly (a string split, a distance), compute it once per element and sort pairs `(key, index)`. Python's `key=` argument does exactly this ("called exactly once for each input record", Python Sorting HOWTO); `functools.cmp_to_key` is the slow way, for old-style comparison functions.

### 2.3 Heaps turn the comparison around

`std::priority_queue` is a **max-heap**: its top is the element that comes last under `comp`. For a min-heap pass `std::greater<int>`, or a lambda that swaps the arguments (`y < x`), as in section 5. Writing `x < y` in that lambda builds the max-heap again: the most common bug in top-K code.

---

## 3. Orders from a rule

### 3.1 Largest Number (LeetCode 179)

Arrange the numbers so that their concatenation is as large as possible. Sorting by value fails: putting 30 before 3 gives `"303"`, but `"330"` is larger. The rule that works compares the two concatenations: `a` goes before `b` when `ab > ba` (as strings).

```cpp
// LeetCode 179: arrange the numbers so that their concatenation is the largest. a goes before b when "ab" is larger than "ba".
std::string largestNumber(const std::vector<int>& nums) {
    std::vector<std::string> s;
    for (int x : nums) s.push_back(std::to_string(x));
    std::sort(s.begin(), s.end(), [](const std::string& a, const std::string& b) { return a + b > b + a; });
    if (s[0] == "0") return "0";                                // the biggest first piece is 0: all pieces are 0
    std::string result;
    for (const std::string& x : s) result += x;
    return result;
}
```

For `[3, 30, 34, 5, 9]` it gives `9534330`; for `[0, 0]` it gives `0` (the check on `s[0]` stops a result such as `"00"`).

Why is this rule a strict weak ordering? Because it compares keys in disguise. For a number `x` with `L` digits let `f(x) = x / (10^L - 1)`; that is the repeating decimal `0.xxxx...` (`f(3) = 0.333...`, `f(30) = 0.3030...`). With `a` having `p` digits and `b` having `q` digits:

`ab > ba`  ⟺  `a * 10^q + b > b * 10^p + a`  ⟺  `a * (10^q - 1) > b * (10^p - 1)`  ⟺  `f(a) > f(b)`.

So the rule is "larger `f` first", a comparison of keys, and all four rules hold. `f` also explains the order: 9 (1.0), 5 (0.555...), 34 (0.3434...), 3 (0.333...), 30 (0.3030...). Whenever a pairwise rule looks suspicious, look for such a key; if you cannot find one, test the rule with section 1.4.

### 3.2 Custom Sort String (LeetCode 791)

Sort the letters of `s` by the position of each letter in `order`; letters that `order` lacks may go anywhere. Give every letter a rank and sort by rank:

```cpp
// LeetCode 791: the letters of s in the order given by `order`; letters that `order` lacks go last (rank 26), in their old order.
std::string customSortString(const std::string& order, std::string s) {
    int rank[26];
    std::fill(rank, rank + 26, 26);
    for (int i = 0; i < (int)order.size(); ++i) rank[order[i] - 'a'] = i;
    std::stable_sort(s.begin(), s.end(), [&](char a, char b) { return rank[a - 'a'] < rank[b - 'a']; });
    return s;
}
```

For `order = "cba"` and `s = "abcd"` the answer is `cbad`. The letters outside `order` all have rank 26, so `stable_sort` leaves them last in their old order. A counting version (count the letters of `s`, then write them out in the order of `order`, then the rest) is O(n) and avoids the comparator altogether: tutorial 04, section 7.1.

---

## 4. Selection: the k-th element without sorting

### 4.1 Quickselect

Sorting everything to read one position wastes work. A partition already tells where the pivot belongs, so only one side can hold index `k`. With the three-way partition of tutorial 04 (section 4.5), one pass splits `a[lo..hi)` into `< pivot | == pivot | > pivot`, and the loop keeps only the part that holds `k`:

```cpp
// Three-way partition of a[lo..hi) around the value `pivot`: a[lo..lt) < pivot, a[lt..gt) == pivot, a[gt..hi) > pivot.
// Returns {lt, gt}.
template <class T>
std::pair<int, int> partition3(std::vector<T>& a, int lo, int hi, const T& pivot) {
    int lt = lo, i = lo, gt = hi;
    while (i < gt) {
        if (a[i] < pivot) std::swap(a[lt++], a[i++]);
        else if (pivot < a[i]) std::swap(a[i], a[--gt]);
        else ++i;
    }
    return {lt, gt};
}

// Quickselect: the element that would be at index k if a[lo..hi) were sorted (lo <= k < hi). Partition around a random
// element, then keep only the side that holds index k. Expected O(n); rearranges a.
template <class T>
T quickSelect(std::vector<T>& a, int lo, int hi, int k, std::mt19937& rng) {
    while (true) {
        const T pivot = a[lo + (int)(rng() % (hi - lo))];
        auto [lt, gt] = partition3(a, lo, hi, pivot);
        if (k < lt) hi = lt;                                    // index k is among the smaller elements
        else if (k >= gt) lo = gt;                              // ... among the larger ones
        else return pivot;                                      // ... or it is in the block of elements equal to the pivot
    }
}
```

Indexes are 0-based: `k = 0` is the minimum. Take `[7, 2, 9, 4, 6, 1, 8]` and `k = 3`, the 4th smallest. The sorted array is `1 2 4 6 7 8 9`, so the answer is 6. Say the random pivot is 7: the partition gives `[2, 1, 4, 6 | 7 | 8, 9]`, `lt = 4`, `gt = 5`. Since `k = 3 < lt`, keep `[0, 4)`. Say the next pivot is 4: `[2, 1 | 4 | 6]`, `lt = 2`, `gt = 3`; now `k >= gt`, so keep `[3, 4)`. One element is left, it is the pivot, and the answer is 6. The three ranges had 7, 4 and 1 elements.

**Cost.** In the usual analysis (one comparison per element per round) the expected number of comparisons is about `2n` for the minimum or maximum and about `2(1 + ln 2) n ≈ 3.39 n` for the median; for `n = 1000` and the median the exact value is 3,336 (a closed form that I checked against a direct calculation for every `n` below 25). The worst case is `n(n - 1)/2`, when every pivot is extreme; with a random pivot that is not a real risk, but the honest claim is "expected O(n)". The three-way partition keeps an array of equal keys at one pass (2n comparisons here, because it spends up to two `<` per element).

### 4.2 Median of medians: a worst case you can prove

To make the worst case linear, choose a pivot that cannot be extreme. Split the elements into groups of 5, sort each group (a constant amount of work) and take its median; the pivot is the **median of those medians**, found by the same function. Half of the group medians are at most the pivot, and each such group has 3 elements at most its median, so at least 3/10 of all elements are at most the pivot. The same holds for "at least". After the partition at most 7/10 of the elements remain:

```cpp
// The same result in O(n) in the worst case. The pivot is the median of the medians of the groups of 5, so every round
// removes at least about 3/10 of the elements. Rearranges a.
template <class T>
T selectMoM(std::vector<T>& a, int lo, int hi, int k) {
    while (hi - lo > 5) {
        std::vector<T> medians;
        for (int g = lo; g < hi; g += 5) {                      // sort each group of 5 and keep its median
            int end = std::min(g + 5, hi);
            insertionSort(a, g, end);
            medians.push_back(a[g + (end - g - 1) / 2]);
        }
        const int m = (int)medians.size();
        const T pivot = selectMoM(medians, 0, m, (m - 1) / 2);  // the median of the medians, found by the same function
        auto [lt, gt] = partition3(a, lo, hi, pivot);
        if (k < lt) hi = lt;
        else if (k >= gt) lo = gt;
        else return pivot;
    }
    insertionSort(a, lo, hi);                                   // at most 5 elements left
    return a[k];
}
```

(`insertionSort` is the one of tutorial 04, section 2.) For the 15 elements `11 3 8 15 6 | 2 14 9 1 12 | 7 13 4 10 5` the sorted groups are `3 6 8 11 15`, `1 2 9 12 14` and `4 5 7 10 13`, with medians 8, 9 and 7. The median of the medians is 8, the pivot: 8 elements are at most 8 and 8 are at least 8, and the guarantee promised 6 each (3 elements from each of the 2 groups whose median is at most 8, and the same on the other side).

**Why groups of 5.** The work is `T(n) <= T(n/5) + T(7n/10) + c n`: one call on the medians and one on what remains. Since `1/5 + 7/10 = 9/10 < 1`, the total is at most `10 c n`, linear. (With the floors of exact counting, the usual textbook bound is `3n/4` for the larger part when `n >= 50`; the argument has the same shape.) With groups of 3 a pivot is guaranteed only 1/3 of the elements on each side, the sizes add up to `1/3 + 2/3 = 1`, and the argument gives only `n log n`. With groups of 7 it works (`1/7 + 5/7 = 6/7`) but sorting the groups costs more.

### 4.3 What it costs, and what the library does

Comparisons (`<` calls) to find the median, `n = 1000`, `k = 499`, counted by the companion file. The first row is the average over 200 random arrays; the others are single runs. They are counts of this library and these implementations, not times:

| Input | quickselect | median of medians | `std::nth_element` | `std::sort` |
|---|---|---|---|---|
| random (average) | 5,076 | 8,722 | 2,797 | 11,909 |
| sorted | 2,733 | 6,951 | 2,514 | 10,621 |
| all equal | 2,000 | 3,495 | 2,022 | 7,243 |

My quickselect pays more than the classical 3,336 because its loop spends up to two `<` per element. Median of medians costs about 1.7 times as much as quickselect on random input and 2.5 times on the sorted one: its guarantee is paid for on every input, and the grouping is not free. On random input a full sort costs more than four times as much as `std::nth_element`. In practice, nobody calls median of medians in a hot loop; its value is the proof that linear worst-case selection exists.

`std::nth_element` is quickselect with the engineering of `std::sort`. In GCC 13.3's `stl_algo.h`, `__introselect` uses the same median-of-three pivot as `std::sort` (tutorial 04, section 9), counts rounds, and after `2 * __lg(n)` rounds switches to heap selection (`__heap_select`); ranges of 3 or fewer elements are finished by insertion sort. The standard asks only for "linear on average", and that fallback is O(n log n), not O(n). After the call, the element at `nth` is the one a full sort would put there, everything before it is not greater, everything after it is not smaller, and **both sides are in no particular order**. If `nth` is the end iterator there is no element to select (the standard promises nothing, and libstdc++ returns at once), so never read `*nth` without checking.

---

## 5. Top K

### 5.1 Four ways

| Method | Time | Extra memory | Good for |
|---|---|---|---|
| sort everything, take `k` | O(n log n) | O(1) | `k` close to `n`, or when you need the full order anyway |
| min-heap of size `k` | O(n log k) | O(k) | streams, small `k`, memory limits |
| select, then sort the `k` winners | O(n + k log k) expected | O(1) | the array is in memory and `k` is moderate |
| counting or buckets | O(n + range) | O(range) | small integer keys, or counts (Top K Frequent: Hashing 02, section 3.2) |

All four in code, for the `k` largest, largest first (`1 <= k <= n`):

```cpp
// The k largest elements of a (1 <= k <= n), largest first. Sort everything: O(n log n).
template <class T>
std::vector<T> topKBySort(std::vector<T> a, int k) {
    std::sort(a.begin(), a.end(), [](const T& x, const T& y) { return y < x; });
    a.resize(k);
    return a;
}

// A min-heap that holds the k largest elements seen so far: O(n log k), and it works on a stream. The heap's own order is
// reversed (its top is the SMALLEST kept element), so the test "is x better than the worst kept?" is  heap.top() < x.
template <class T>
std::vector<T> topKByHeap(const std::vector<T>& a, int k) {
    auto later = [](const T& x, const T& y) { return y < x; };
    std::priority_queue<T, std::vector<T>, decltype(later)> heap(later);
    for (const T& x : a) {
        if ((int)heap.size() < k) heap.push(x);
        else if (heap.top() < x) { heap.pop(); heap.push(x); }
    }
    std::vector<T> out(heap.size());
    for (int i = (int)out.size() - 1; i >= 0; --i) { out[i] = heap.top(); heap.pop(); }   // smallest first, so fill from the back
    return out;
}

// Partition around the k-th largest with nth_element, then sort only the k winners: O(n + k log k) on average.
template <class T>
std::vector<T> topKBySelect(std::vector<T> a, int k) {
    auto bigger = [](const T& x, const T& y) { return y < x; };
    std::nth_element(a.begin(), a.begin() + (k - 1), a.end(), bigger);
    a.resize(k);
    std::sort(a.begin(), a.end(), bigger);
    return a;
}

// The library's heap method: partial_sort puts the k largest first, in order, and leaves the rest unspecified.
template <class T>
std::vector<T> topKByPartialSort(std::vector<T> a, int k) {
    std::partial_sort(a.begin(), a.begin() + k, a.end(), [](const T& x, const T& y) { return y < x; });
    a.resize(k);
    return a;
}
```

The heap holds the **best `k` so far**, and its top is the worst of them, so a new value enters only when it beats the top: that is why the heap's comparison is reversed (section 2.3) and the test reads `heap.top() < x`. `std::partial_sort` is the same heap method done by the library (in GCC's header it is `__heap_select` followed by `__sort_heap`).

Comparisons for `n = 100,000` random values, by `k` (counts of GCC 13.3's library, not times):

| `k` | sort all | heap of size `k` | `nth_element` + sort `k` | `partial_sort` |
|---|---|---|---|---|
| 10 | 1,995,982 | 100,501 | 139,438 | 100,400 |
| 1,000 | 1,995,982 | 165,466 | 157,059 | 160,054 |
| 50,000 | 1,995,982 | 1,493,108 | 1,127,176 | 1,426,347 |

For small `k` the heap pays about one comparison per element (a new value rarely beats the top), so it is close to `n`. As `k` grows, selection pulls ahead and the heap's `log k` shows. Real time also depends on memory access: a heap jumps around, a partition scans, so measure before you trust counts.

### 5.2 Choosing

- The data arrives as a stream, or does not fit in memory: **heap of size `k`**. It is also the answer to "the k-th largest after every insertion".
- The array is in memory and you need the `k` largest: **`nth_element`**, then sort only those.
- You need only the k-th value (the median, a percentile): **`nth_element`** or quickselect; do not sort.
- `k` is close to `n`: select the `n - k` smallest instead and drop them.
- The keys are small integers or counts: **counting** (a histogram), O(n).
- The input must not be changed: copy it first. Quickselect and `nth_element` rearrange the array.

Pitfalls that cost marks: `nth_element(a.begin(), a.begin() + k, ...)` finds the (k+1)-th, so the k-th (1-based) is at `begin() + (k - 1)`; a heap that should be a min-heap built with `<` (section 2.3); and `k = 0`, where `begin() + (k - 1)` is before the array.

LeetCode 215 asks for the k-th largest, which is the last of the `k` winners. LeetCode 973 asks for the `k` points closest to the origin: order them by `x*x + y*y` (no square root is needed, and the squares fit in an `int` for coordinates up to 10^4). LeetCode 347 is in Hashing 02.

---

## 6. Sorting intervals

### 6.1 Merge intervals (LeetCode 56)

Merge all overlapping closed intervals `[start, end]`. Sort by **start**; then an interval either starts inside the block built so far (it extends the block) or after it (it starts a new block), because everything earlier has a smaller start:

```cpp
struct Interval {
    int start, end;
};

// Merge overlapping closed intervals [start, end]; [1, 4] and [4, 5] share the point 4, so they merge. Sort by start, then
// sweep: an interval that starts inside the last merged one extends it, any other starts a new one.
std::vector<Interval> mergeIntervals(std::vector<Interval> v) {
    std::sort(v.begin(), v.end(), [](const Interval& a, const Interval& b) { return a.start < b.start; });
    std::vector<Interval> out;
    for (const Interval& iv : v) {
        if (!out.empty() && iv.start <= out.back().end) out.back().end = std::max(out.back().end, iv.end);
        else out.push_back(iv);
    }
    return out;
}
```

`[1,3] [2,6] [8,10] [15,18]` becomes `[1,6] [8,10] [15,18]`. The LeetCode statement says that intervals that only touch are merged: `[1,4] [4,5]` becomes `[1,5]`, which is the `<=` in `iv.start <= out.back().end`; `[1,4] [5,6]` stay apart. The cost is the sort: O(n log n); the sweep is O(n).

### 6.2 Meeting rooms (LeetCode 253)

How many rooms do the meetings need? Sort the **starts and the ends separately**. Walk through the starts: if the earliest-ending meeting is over by the time this one starts, reuse its room, otherwise open a new one:

```cpp
// The fewest rooms for meetings [start, end) (half-open: a meeting may start at the moment another one ends). Sort the
// starts and the ends separately; each start either reuses a room that is already free or opens a new one.
int minRooms(const std::vector<Interval>& v) {
    std::vector<int> starts, ends;
    for (const Interval& iv : v) { starts.push_back(iv.start); ends.push_back(iv.end); }
    std::sort(starts.begin(), starts.end());
    std::sort(ends.begin(), ends.end());
    int rooms = 0, freed = 0;                                   // freed: how many meetings, in order of ending, have been reused
    for (int s : starts) {
        if (s < ends[freed]) ++rooms;                           // even the earliest-ending meeting is still running: new room
        else ++freed;                                           // it is over: reuse its room
    }
    return rooms;
}
```

This sorting is allowed because only counts matter: at any moment, the rooms in use are the starts so far minus the ends so far, and which end belongs to which start does not matter. Here meetings are half-open, `[start, end)`: a meeting may start when another ends, which is the strict `<`. `[0,30] [5,10] [15,20]` needs 2 rooms, `[7,10] [2,4]` needs 1, `[0,30] [30,60]` needs 1, and `[1,5] [2,6] [3,7] [8,9]` needs 3. If a problem says that touching meetings clash, change the test to `<=` (Problem B3).

### 6.3 Which key?

The sort key decides whether the algorithm is correct:

| Goal | Sort by | Why |
|---|---|---|
| merge overlapping intervals; insert one; count the rooms | start (rooms: starts and ends apart) | each interval is compared only with what came before |
| keep as many non-overlapping intervals as possible; fewest arrows for balloons; fewest removals | **end** | the interval that ends first leaves the most room for the rest |

Sorting by start breaks the second kind: for `[1,100] [2,3] [4,5]` the earliest start picks `[1,100]` and nothing else (1 interval), while the earliest end picks `[2,3]` and `[4,5]` (2). I checked "earliest end first" against brute force over all subsets on random small inputs. The proof (an exchange argument) and the problems LeetCode 435, 57 and 452 belong to the Greedy and intervals tutorials (area 09).

---

## 7. Mistakes that cost points

1. **`<=` or `>=` in a comparator.** Undefined behaviour; a crash with 17 equal values (section 1.3). Use `<` and `>`.
2. **Two keys joined with `||`.** True in both directions; compare the second key only when the first ties (section 1.2).
3. **Subtracting to compare.** `a - b` used as a bool is true for every pair that differs, and `a - b < 0` overflows for values near plus and minus two billion. Write `a < b`.
4. **A min-heap built with `<`.** `std::priority_queue` is a max-heap by default; the top-K test then keeps the wrong end (section 2.3, Problem B4).
5. **Reading the wrong place after `nth_element`.** The k-th (1-based) is at `begin() + (k - 1)`; both sides are unsorted; and the array was rearranged (section 4.3).
6. **The heap for the k smallest.** It is the mirror of the k-largest heap: its top must be the largest of the kept values (Problem 4).
7. **Sorting intervals by start when the goal is "as many as possible".** Sort by end (section 6.3).
8. **Not reading whether touching counts.** `[1,4]` and `[4,5]` merge in LeetCode 56 (`<=`), but a meeting may start when another ends (`<`, section 6.2, Problem B3).
9. **NaN in a sort of doubles.** `<` stops being a strict weak ordering; put the NaNs last on purpose (section 1.3).
10. **A pairwise rule that was never tested.** If you cannot find the key behind it (section 3.1), run the check of section 1.4 on a sample.

---

## 8. Questions you will be asked

**What does `std::sort` need from its comparator, and what if it fails?** A strict weak ordering: irreflexive, asymmetric, transitive, with transitive equivalence; comparing keys with `<` gives all four. If not, the behaviour is undefined: a wrong order or a crash (section 1.3). `<=` is the classic case: libstdc++'s unchecked partition loops run off the end of the array when there are enough equal values.

**Sort by grade descending, then name ascending.** `if (a.grade != b.grade) return a.grade > b.grade; return a.name < b.name;`, or `std::tie` with the two sides of the descending key swapped. With stable sorts only: sort by the second key first, then by the first (tutorial 04, section 8).

**Find the k-th largest.** Sort: O(n log n). Heap of size k: O(n log k), works on a stream. Quickselect: expected O(n), worst case O(n²). Median of medians: O(n) worst case, large constant. `std::nth_element`: linear on average.

**Why groups of 5 in median of medians?** The pivot has at least 3/10 of the elements on each side, so the work is `T(n/5) + T(7n/10) + O(n)`, and `1/5 + 7/10 < 1` gives O(n). With groups of 3 the sizes add up to 1, and the argument gives n log n.

**Heap or `nth_element` for the top K?** Heap for a stream or little memory: O(n log k); its top is the value to throw out when a better one arrives. `nth_element` and then sorting the k winners when everything is in memory: O(n + k log k) expected.

**Merge overlapping intervals; and why may the room count sort starts and ends separately?** Sort by start, sweep, extend the last block while the next start is at most its end: O(n log n). For rooms only the number of meetings running at each moment matters, not which meeting ends when.

**Why is the Largest Number comparator valid?** `ab > ba` is the same as comparing the keys `x / (10^len(x) - 1)`, and comparing keys is a strict weak ordering.

---

## 9. Problems

**How to use this section.** Try each problem for 15 minutes (easy) or 30 minutes (harder) before reading anything. If you are stuck, read only the **How** lines, then try again, and only then open the answer at the bottom. Log each attempt in the [practice log](../practice_log.md).

### On paper

**Problem A: Trace.** Run `partition3` on `[5, 2, 8, 5, 9, 1, 5]` with the pivot 5: write the array and `(lt, gt)` after the call. Then say, for `k` = 0, 3 and 6, which range `quickSelect` keeps or what it returns.
*Where:* paper. *How:* section 4.1. A smaller value swaps to the front and `i` advances; a larger one swaps to the back and `i` stays.

**Problem B: Find the bug.** Each change breaks one function of the companion file. For each, give an input that shows it and say what the function returns.

```cpp
// 1. in largestNumber, the line after the sort was deleted:
//        if (s[0] == "0") return "0";
// 2. in quickSelect, the middle test became:
        else if (k > gt) lo = gt;
// 3. in minRooms, the test became:
        if (s <= ends[freed]) ++rooms;
// 4. in topKByHeap, the heap's comparison became:
    auto later = [](const T& x, const T& y) { return x < y; };
```
*Where:* paper. *How:* 1: only zeros. 2: the case `k == gt`. 3: meetings that touch. 4: which value is on top of the heap now?

**Problem C: Strict weak ordering?** For each comparator on the ints 0 to 29 (on -5 to 5 for the last two), say yes or no and give the reason. (a) `a % 3 < b % 3`. (b) `a / 10 <= b / 10`. (c) `a % 2 == 0 && b % 2 == 1`. (d) `a % 2 == 0 || b % 2 == 1`. (e) `abs(a) < abs(b)`. (f) `a * a < b * b + 1`.
*Where:* paper. *How:* check `comp(a, a)` first; then look for a key that the comparison is hiding.

**Problem D: The 3/10.** With groups of 5 and `n = 100`: at least how many elements are at most the pivot, at least how many are at least the pivot, and how large can the next range be? Repeat for groups of 3 and `n = 99`, and say what recurrence that gives.
*Where:* paper. *How:* section 4.2. Count the groups; the pivot is the median of that many medians; each group whose median is on the right side brings 3 (or 2) elements.

**Problem E: Which method?** Name the best choice and say why: (1) a stream of 10^9 numbers, keep the 100 largest; (2) 10^6 numbers in memory, the median; (3) 10^7 numbers, all between 0 and 1000, the 10 largest; (4) 10^6 numbers, everything except the 3 smallest, in any order; (5) the k-th largest of a list that grows, asked after every insertion; (6) the 5 points closest to the origin out of 10^4, in any order.
*Where:* paper. *How:* section 5.2: ask whether the data streams, how large the range is, and whether the order of the answer matters.

### In code

**Problem 1: Kth Largest Element in an Array** (LeetCode 215, medium)
*Where:* leetcode.com/problems/kth-largest-element-in-an-array. *Goal:* solve it four ways: sort, a heap of size k, quickselect with a random pivot and the three-way partition, and counting (the values lie between -10^4 and 10^4); compare the time on 10^5 values.
*How:* sections 4 and 5. The k-th largest is the element at index `n - k` of the sorted array. Counting scans the histogram from the top.

**Problem 2: Largest Number** (LeetCode 179, medium)
*Where:* leetcode.com/problems/largest-number. *Goal:* return the arrangement as a string; then check your comparator against all orders of up to 6 numbers.
*How:* section 3.1. Compare `a + b` with `b + a`. The numbers go up to 10^9, so use strings, and handle the all-zero input.

**Problem 3: Custom Sort String** (LeetCode 791, medium)
*Where:* leetcode.com/problems/custom-sort-string. *Goal:* write the rank sort, then a version that sorts nothing.
*How:* section 3.2. Counting version: count the letters of `s`, write them out in the order of `order`, then the rest.

**Problem 4: K Closest Points to Origin** (LeetCode 973)
*Where:* leetcode.com/problems/k-closest-points-to-origin. *Goal:* three versions: sort by squared distance, a heap of size k, and `nth_element`.
*How:* section 5. No square root is needed. For the k *smallest* the heap's top must be the largest kept distance, the opposite of the heap in 5.1.

**Problem 5: Merge Intervals** (LeetCode 56, medium)
*Where:* leetcode.com/problems/merge-intervals. *Goal:* O(n log n) time.
*How:* section 6.1. Sort by start; a block's end becomes the larger of the two ends, never just the new one.

**Problem 6: Meeting Rooms II** (LeetCode 253, medium)
*Where:* leetcode.com/problems/meeting-rooms-ii (as far as I know it needs LeetCode Premium; the statement is in section 6.2), or CSES Restaurant Customers. *Goal:* the fewest rooms, in two ways: starts and ends sorted apart, and a min-heap of the end times of the rooms in use.
*How:* section 6.2. Heap version: for each meeting in order of start, pop the top while it is at most the start, then push the end; the answer is the largest heap size.

**Problem 7: Median of medians** (your own editor)
*Where:* your editor. *Goal:* write `selectMoM` with the group size as a parameter and a counting wrapper type for `<`. For `n = 1000`, compare the counts of quickselect, median of medians and `std::nth_element` on random, sorted and all-equal arrays; then try groups of 3 and of 7.
*How:* sections 4.2 and 4.3. Check every answer against `std::sort`. The median of a group of `g` elements is at index `(g - 1) / 2` after sorting it.

**Problem 8: Break `std::sort`** (your own editor)
*Where:* your editor. *Goal:* reproduce section 1.3 on your machine (17 equal ints and `<=` under AddressSanitizer; 1000 small random values without it; then with `-D_GLIBCXX_DEBUG`); then write `isStrictWeakOrder` and test five comparators of your own.
*How:* sections 1.3 and 1.4. The behaviour is undefined, so write down what your compiler and library did.

**More practice:** Top K Frequent Words (692), Kth Largest Element in a Stream (703), Relative Sort Array (1122), Queue Reconstruction by Height (406), Wiggle Sort II (324), Find the Kth Largest Integer in the Array (1985); Insert Interval (57), Non-overlapping Intervals (435) and Minimum Number of Arrows to Burst Balloons (452) after the Greedy tutorials; CSES Restaurant Customers. Top K Frequent Elements (347) is in Hashing 02.

---

## Answers

### Problem A

`[5, 2, 8, 5, 9, 1, 5]` becomes `[2, 1, 5, 5, 5, 9, 8]` with `lt = 2` and `gt = 5`: the steps are 5 (equal, skip), 2 (swap to the front), 8 (swap with the last), 5 and 5 (skip), 9 (swap with the new last) and 1 (swap to the front). For `k = 0` the range `[0, 2)` is kept (`k < lt`); for `k = 3` the call returns 5 (`lt <= k < gt`); for `k = 6` the range `[5, 7)` is kept (`k >= gt`).

### Problem B

1. `[0, 0]` returns `"00"`; the answer is `"0"`. Without the check the result is the zeros joined.
2. `[1, 2]` with `k = 1` and the random pivot 1: the partition gives `lt = 0`, `gt = 1`; `k > gt` is false, so the function returns the pivot 1, but the answer is 2. The test must be `k >= gt`: when `k == gt` the wanted element is the first one above the pivot.
3. `[0, 30]` and `[30, 60]` need 2 rooms now, not 1. This version is right when a statement says that meetings sharing an end point clash (closed intervals).
4. The comparison now builds a max-heap, so its top is the largest kept value and nothing larger can enter. On `[5, 1, 4, 2, 3]` with `k = 2` it returns `1 5` (the first two values, in the wrong order) instead of `5 4`.

### Problem C

(a) Yes: it compares the key `a % 3`. (b) No: `comp(a, a)` is true. (c) Yes: it compares the key "is odd". (d) No: `comp(a, a)` is true for every even `a`. (e) Yes: it compares the key `|a|`. (f) No: for integers `a * a < b * b + 1` means `a * a <= b * b`, so `comp(a, a)` is true.

### Problem D

Groups of 5, `n = 100`: 20 groups; the pivot is the 10th smallest median, so at least 10 groups have a median at most the pivot, each with 3 elements at most it: at least 30 elements are at most the pivot. At least 11 medians are at least the pivot: at least 33 elements are at least the pivot. The next range has at most `100 - 30 = 70` elements. Groups of 3, `n = 99`: 33 groups, the pivot is the 17th smallest median; at least 17 groups with 2 elements each: at least 34 elements on each side, so at most 65 remain. The recurrence is `T(n/3) + T(2n/3) + O(n)`: the fractions add up to 1, and the solution is `O(n log n)`.

### Problem E

1. A heap of size 100: O(n log 100) time, O(100) memory, one pass over the stream.
2. `nth_element` (or quickselect): the median in expected O(n); do not sort.
3. Counting: a histogram of 1001 cells, O(n + 1000); read it from the top.
4. `nth_element` with `nth` at index 3: the elements after it are the answer, in no order; no sort needed.
5. A min-heap of size k: the top is the answer after each insertion, O(log k) each.
6. A max-heap of size 5 on the squared distance (or `nth_element`, or a sort: at this size all three are fast). Note that the heap's top is the largest kept distance.

### Problems 1 to 8

1. `nth_element(a.begin(), a.begin() + (k - 1), a.end(), std::greater<int>())` and read `a[k - 1]`; or a min-heap of size k whose top is the answer; or `quickSelect` at index `n - k`; or a histogram of 20,001 cells and a scan from the top that subtracts counts until `k` is reached.
2. Strings sorted with `a + b > b + a`; return `"0"` if the first string is `"0"`. Brute force: try every permutation (`std::next_permutation` on a sorted copy) and keep the largest concatenation (all have the same length, so compare them as text).
3. Rank 26 for letters that `order` lacks, `stable_sort` by rank. Counting version: 26 counts, a pass over `order`, then a pass over the letters that are not in it.
4. Sort by `x*x + y*y` and cut at `k`; or a max-heap of (distance, index) that pops its top whenever it holds more than `k`; or `nth_element` at `begin() + (k - 1)` by distance.
5. Sort by start; if the next start is at most the last end, set the last end to the larger of the two ends, else push the interval.
6. Starts and ends sorted apart, as in 6.2. Heap version: sort by start; pop while the top is at most the start; push the end; the answer is the largest size the heap reaches.
7. Loop while `hi - lo > g`, sort groups of `g`, take each median at `(len - 1) / 2`, recurse on the medians. Groups of 3 and 7 are both correct; what changes is the guarantee (sizes adding up to 1 for 3, which gives n log n, and to 6/7 for 7, which is linear) and the cost of sorting the groups.
8. Copy `isStrictWeakOrder` and call it on at most 30 values (the work is the cube of the sample size). Crash messages differ between machines; only the debug-mode abort is reliable.

---

## Cheat sheet

| Task | Tool | Time |
|---|---|---|
| sort by several keys | one comparator, the second key only on ties; or `std::tie` | O(n log n) |
| the order, not the values | sort the indexes (`stable_sort`) | O(n log n) |
| k-th element, median | `std::nth_element` or quickselect | expected O(n) |
| k-th element, worst-case guarantee | median of medians | O(n), large constant |
| k largest of a stream | min-heap of size k | O(n log k) |
| k largest, data in memory | `nth_element`, then sort the k | expected O(n + k log k) |
| small integer keys or counts | counting | O(n + range) |
| merge overlapping intervals | sort by start, one sweep | O(n log n) |
| fewest rooms | sort starts and ends apart | O(n log n) |
| most non-overlapping intervals | sort by end, take greedily | O(n log n) |

- **Comparator:** compare keys with `<`; never `<=`; the second key only when the first ties; a descending key swaps the sides.
- **Test one:** irreflexive, asymmetric, transitive, transitive equivalence; run the checker of 1.4 on a sample.
- **Heaps:** `std::priority_queue` is a max-heap; for the k largest keep a min-heap and replace the top when a value beats it; for the k smallest, the reverse.
- **Selection:** `nth_element` leaves both sides unsorted; index `k - 1` for the k-th; groups of 5 give `1/5 + 7/10 < 1`.
- **Intervals:** merge and rooms by start; "most intervals" by end; decide first whether touching counts.

**Sources** (checked while writing this tutorial):
- LeetCode statements: [215](https://leetcode.com/problems/Kth-largest-element-in-an-array) and [56](https://leetcode.com/problems/merge-intervals) (on leetcode.com, medium; 56 merges intervals that touch), [179](https://leetcode.doocs.org/en/lc/179/) and [791](https://leetcode.doocs.org/en/lc/791/) (a mirror, medium), [973](https://codeharborhub.github.io/dsa/solutions/lc-solutions/0900-0999/k-closest-points-to-origin) (a mirror that states no difficulty, so none is given here), [253](https://algo.monster/liteproblems/253) (a mirror, medium; it does not say whether the problem is Premium). CSES Restaurant Customers: listed under Sorting and Searching in this [analysis](https://phuongdinh1411.github.io/cses-analyses/problem_soulutions/sorting_and_searching/restaurant_customers_analysis).
- C++: cppreference, [`std::nth_element`](https://en.cppreference.com/cpp/algorithm/nth_element) (linear on average; nothing is promised when `nth` is the end) and [Compare](https://en.cppreference.com/cpp/named_req/Compare) (the four rules; this page would not open on the final check). libstdc++: the header `bits/stl_algo.h` of GCC 13.3, read on the machine used to write this tutorial.
- Selection: [MIT 6.046J, lecture 6](https://www.ocw.mit.edu/courses/6-046j-introduction-to-algorithms-sma-5503-fall-2005/ca4e5673e2b64eacc5691cda564cba49_lec6.pdf) (groups of 5, the 3n/4 bound); *Introduction to Algorithms* (CLRS), chapter 9; Blum, Floyd, Pratt, Rivest and Tarjan, "Time bounds for selection", 1973.
- Java: [JDK-8134576](https://bugs.openjdk.org/browse/JDK-8134576) (the contract exception appears on some inputs and not on others; closed as Won't Fix). Python: [Sorting Techniques](https://docs.python.org/3/howto/sorting.html) (the key function is called once per record; `functools.cmp_to_key`).

**Next:** this closes the binary search and sorting area, so solve its problem lists before moving on. The next tutorial is Linked lists 03, doubly and circular lists (the sentinel node behind `std::list` and the Linux kernel's `list_head`). Say "next" when you are ready.
