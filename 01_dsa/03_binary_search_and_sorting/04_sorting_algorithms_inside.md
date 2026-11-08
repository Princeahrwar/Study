# DSA 03.04: Sorting Algorithms Inside

*Part of the [Binary search and sorting plan](00_plan.md) · Time: about 6 hours (2.5 reading, 3.5 problems) · You need: [Binary search templates](01_binary_search_templates.md) (half-open ranges `[lo, hi)`) and the Dutch national flag from the [two pointers tutorial](../01_arrays_and_strings/02_two_pointers.md) · Companion code: [`04_sorting_algorithms_inside_demo.cpp`](04_sorting_algorithms_inside_demo.cpp)*

By the end of this tutorial you can:

- write insertion sort, merge sort, quicksort (two partitions, and the three-way one for equal keys) and heapsort from memory, and trace each on a small array;
- say what each costs in comparisons, memory and stability, and why;
- prove that no comparison sort beats n log n, and name the sorts that avoid comparing and when they win;
- explain what `std::sort`, the Linux kernel and other libraries do inside;
- sort data that does not fit in memory.

The companion file (C++20) holds every function below and checks it against `std::sort` and `std::stable_sort` on every small input (749,238 checks). It is short on purpose: the tutorial carries the examples and the problems.

---

## 1. What a sort promises

A sort rearranges a sequence into non-decreasing order, using a comparison `a < b` (everything here uses only `<`). Three properties tell the sorts apart:

- **Cost**: comparisons and moves, as a function of `n`, in the best, average and worst case.
- **Memory**: *in place* means O(1) extra memory (a recursion stack of O(log n) frames is usually still called in place); otherwise the sort needs a buffer of O(n).
- **Stability**: a sort is *stable* if elements that compare equal keep their original order. Take the records `(Ann, B), (Bob, A), (Cy, B), (Di, A)` sorted by grade. A stable sort must answer `Bob, Di, Ann, Cy`: Bob before Di and Ann before Cy, as in the input. An unstable sort may answer `Di, Bob, Ann, Cy`, which is also sorted by grade.

Ranges are half-open, `a[lo..hi)`, as in tutorial 01. We build the sorts in order of how they work: insertion (grow a sorted prefix), merge (sort halves, then merge), quick (split by a pivot), heap (select the maximum again and again); then the n log n limit, the sorts that avoid it, stability, what libraries do, and sorting from disk.

---

## 2. Insertion sort

Keep a sorted prefix. Take the next element and move it left past every larger element:

```cpp
// Sort a[lo..hi) by growing a sorted prefix: each new element moves left past the larger ones. Equal elements do not
// pass each other (strict <), so it is stable. It makes one move per inversion, so it is fast on nearly sorted input.
template <class T>
void insertionSort(std::vector<T>& a, int lo, int hi) {
    for (int i = lo + 1; i < hi; ++i) {
        T x = a[i];
        int j = i;
        while (j > lo && x < a[j - 1]) { a[j] = a[j - 1]; --j; }   // shift the larger elements right
        a[j] = x;
    }
}
```

On `[2, 4, 1, 3, 5]`: the prefix `[2]` takes 4 without moving it; then `1` moves past 4 and 2 (two moves), giving `[1, 2, 4]`; then `3` moves past 4 (one move); then `5` stays. Three moves in total.

An *inversion* is a pair `i < j` with `a[i] > a[j]`. The array above has three: `(2, 1)`, `(4, 1)`, `(4, 3)`. Each move removes exactly one inversion, and a sorted array has none, so **the number of moves equals the number of inversions**. That makes insertion sort O(n) on sorted input, about n²/4 moves on random input and n(n-1)/2 on reversed input. It is stable (the strict `<` stops it from passing an equal element), uses O(1) memory and has almost no overhead, which is why libraries use it for the last few elements of a part (section 9). Selection sort and bubble sort have no such use; they exist for teaching.

---

## 3. Merge sort

### 3.1 The idea

Split the array in half, sort each half, then **merge** the two sorted halves into one: repeatedly compare the first unused element of each half and take the smaller. For `[5, 2, 9, 1, 5, 6]`:

```
[5 2 9 1 5 6]
[5 2 9]   [1 5 6]
[5] [2 9]   [1] [5 6]
```

The merges go bottom up: `[2]+[9]`, `[5]+[2, 9]` gives `[2, 5, 9]`, `[5]+[6]`, `[1]+[5, 6]` gives `[1, 5, 6]`, and last the merge of `[2, 5, 9]` with `[1, 5, 6]`:

| step | left head | right head | what happens | output so far |
|---|---|---|---|---|
| 1 | 2 | 1 | right is smaller, take it | 1 |
| 2 | 2 | 5 | left is smaller | 1 2 |
| 3 | 5 | 5 | tie: take the **left** one | 1 2 5 |
| 4 | 9 | 5 | right is smaller | 1 2 5 5 |
| 5 | 9 | 6 | right is smaller | 1 2 5 5 6 |
| end | 9 | none | right is used up, copy the rest | 1 2 5 5 6 9 |

The whole sort made 10 comparisons (1, 2, 1, 1 and 5 for the five merges). Step 3 is why merge sort is stable: on a tie the left element, which came first in the input, goes first.

### 3.2 The code

```cpp
// Merge the sorted runs a[lo..mid) and a[mid..hi) through tmp. On a tie the left element goes first: stable.
template <class T>
void mergeRuns(std::vector<T>& a, std::vector<T>& tmp, int lo, int mid, int hi) {
    int i = lo, j = mid, k = lo;
    while (i < mid && j < hi) tmp[k++] = (a[j] < a[i]) ? a[j++] : a[i++];
    while (i < mid) tmp[k++] = a[i++];
    while (j < hi) tmp[k++] = a[j++];
    std::copy(tmp.begin() + lo, tmp.begin() + hi, a.begin() + lo);
}

// Top-down merge sort of a[lo..hi); tmp is a buffer as long as a. Stable, O(n log n) always, O(n) extra memory.
template <class T>
void mergeSort(std::vector<T>& a, std::vector<T>& tmp, int lo, int hi) {
    if (hi - lo < 2) return;
    int mid = lo + (hi - lo) / 2;
    mergeSort(a, tmp, lo, mid);
    mergeSort(a, tmp, mid, hi);
    mergeRuns(a, tmp, lo, mid, hi);
}
```

The buffer `tmp` is allocated once, outside the recursion, and has the same length as `a`.

### 3.3 Cost

A merge of `m` elements makes at most `m - 1` comparisons, so the worst case follows `W(n) = W(⌊n/2⌋) + W(⌈n/2⌉) + n - 1`: **8,977 comparisons for n = 1000**, against the lower bound of 8,530 (section 6). The input never changes the shape of the recursion, so merge sort is O(n log n) in every case; the price is the O(n) buffer. A sorted input costs about half of the worst case (4,932), because every merge ends as soon as the left run is used up.

### 3.4 Bottom-up, and linked lists

Instead of recursing, merge runs of width 1, then 2, then 4, until one run is left. On a **linked list** this needs no buffer, because a merge just relinks nodes: O(n log n) time and O(1) extra memory (the follow-up of LeetCode 148). The top-down version on a list uses O(log n) stack.

### 3.5 Counting inversions on the way

When the merge takes an element from the right run, it is smaller than **every element still waiting in the left run**: `mid - i` inversions end here.

```cpp
// The number of inversions (pairs i < j with a[i] > a[j]) of a[lo..hi), counted while merge-sorting it. When the next
// element comes from the right run, it is smaller than every element still waiting in the left run: mid - i inversions.
template <class T>
long long countInversions(std::vector<T>& a, std::vector<T>& tmp, int lo, int hi) {
    if (hi - lo < 2) return 0;
    int mid = lo + (hi - lo) / 2;
    long long count = countInversions(a, tmp, lo, mid) + countInversions(a, tmp, mid, hi);
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
```

For `[2, 4, 1, 3, 5]` the last merge is `[2, 4]` with `[1, 3, 5]`: taking `1` counts 2, taking `3` counts 1 (only 4 is larger), total 3, as in section 2. The same idea answers LeetCode 315 and 493 (Problems 4 and 5).

---

## 4. Quicksort

### 4.1 The idea

Pick a **pivot**, *partition* the array so that smaller elements come before larger ones, then sort the two sides independently. There is no merge and no buffer. An even split gives O(n log n); a split into `n - 1` and `0` gives O(n²).

### 4.2 Lomuto's partition

Use the last element as the pivot. Keep an index `i` such that `a[lo..i)` holds the elements smaller than the pivot; scan with `j` and swap each smaller element to the front; finally put the pivot at `i`:

```cpp
// Lomuto partition of a[lo..hi), pivot = the last element. Returns the pivot's final index p:
// a[lo..p) < pivot, a[p] == pivot, a[p + 1..hi) >= pivot.
template <class T>
int partitionLomuto(std::vector<T>& a, int lo, int hi) {
    const T pivot = a[hi - 1];
    int i = lo;                                                 // a[lo..i) holds the elements smaller than the pivot
    for (int j = lo; j < hi - 1; ++j)
        if (a[j] < pivot) std::swap(a[i++], a[j]);
    std::swap(a[i], a[hi - 1]);                                 // the pivot goes between the two parts
    return i;
}
```

On `[3, 8, 2, 5, 1, 4]` (pivot 4):

| j | a[j] | smaller than 4? | array after | i |
|---|---|---|---|---|
| 0 | 3 | yes, swap with itself | 3 8 2 5 1 4 | 1 |
| 1 | 8 | no | 3 8 2 5 1 4 | 1 |
| 2 | 2 | yes, swap with `a[1]` | 3 2 8 5 1 4 | 2 |
| 3 | 5 | no | 3 2 8 5 1 4 | 2 |
| 4 | 1 | yes, swap with `a[2]` | 3 2 1 5 8 4 | 3 |
| end | | pivot goes to `a[3]` | 3 2 1 4 8 5 | |

The pivot 4 is now in its final place, index 3, and `[3, 2, 1]` and `[8, 5]` are sorted on their own. The sort itself recurses into the **smaller** part and loops on the larger one, which keeps the recursion at most log2 n calls deep (section 4.6):

```cpp
// Quicksort with the Lomuto partition. Recurse into the smaller part and loop on the larger: the recursion never goes more
// than log2(n) calls deep, even when the running time is quadratic. Not stable.
template <class T>
void quickSortLomuto(std::vector<T>& a, int lo, int hi) {
    while (hi - lo > 1) {
        int p = partitionLomuto(a, lo, hi);
        if (p - lo < hi - (p + 1)) { quickSortLomuto(a, lo, p); lo = p + 1; }
        else { quickSortLomuto(a, p + 1, hi); hi = p; }
    }
}
```

### 4.3 Hoare's partition

Two indexes start outside the range and move toward each other: `i` from the left stops at an element that is not smaller than the pivot, `j` from the right stops at an element that is not larger; if they have not crossed, swap the two and go on:

```cpp
// Hoare partition of a[lo..hi) (at least two elements), pivot = the value of the middle element. Two indexes move toward
// each other and swap the pairs that are on the wrong side. Returns j with a[lo..j] <= pivot <= a[j + 1..hi), both non-empty.
template <class T>
int partitionHoare(std::vector<T>& a, int lo, int hi) {
    const T pivot = a[lo + (hi - lo - 1) / 2];
    int i = lo - 1, j = hi;
    while (true) {
        do ++i; while (a[i] < pivot);                           // stops at an element >= pivot
        do --j; while (pivot < a[j]);                           // stops at an element <= pivot
        if (i >= j) return j;
        std::swap(a[i], a[j]);
    }
}
```

The sort around it is the loop of `quickSortLomuto` with the parts `[lo, j + 1)` and `[j + 1, hi)`.

On the same array the pivot is the middle element, `a[2] = 2`. `i` stops at 3 (index 0), `j` at 1 (index 4): swap, giving `1 8 2 5 3 4`. `i` stops at 8 (index 1), `j` at 2 (index 2): swap, giving `1 2 8 5 3 4`. Then `i` stops at index 2 and `j` at index 1: they have crossed, the result is `j = 1`, and the parts are `[1, 2]` and `[8, 5, 3, 4]`. The pivot is not placed for good; it ends up somewhere in the left part. Two details matter: the pivot must not be the **last** element (Problem B2), and both scans stop on elements equal to the pivot, which keeps the parts balanced when many keys are equal (4.5). Lomuto's partition is the shorter one to get right in an interview; Hoare's is the kind libstdc++ uses (section 9).

### 4.4 Pivot choice and the worst case

With the last element as the pivot, a **sorted** input is the worst case: the pivot is the largest, every split is `n - 1` and `0`, and the comparisons add up to `n(n - 1)/2`, which is 499,500 for n = 1000 (table in section 6.2). A *random* pivot makes a bad split unlikely: on average `2(n + 1)H_n - 4n` comparisons, where `H_n` is the n-th harmonic number; that is 2 n ln n ≈ 1.39 n log2 n for large `n`, and 10,986 for n = 1000. The **median of three** (first, middle, last) fixes sorted and reversed input for free, but crafted input can still fool it; introsort (section 9) guards against that.

### 4.5 Equal keys and the three-way partition

With many equal keys, Lomuto's partition sends every equal element to the same side: 1000 equal keys give splits of `n - 1` and `0` again, 499,500 comparisons. Hoare's partition swaps equal elements and splits in the middle, so it stays at n log n. The cleanest fix is the **three-way partition**, the Dutch national flag of tutorial 01.02: one pass makes `< pivot | == pivot | > pivot`, and the middle block is final.

```cpp
// Three-way quicksort: a random pivot value splits a[lo..hi) into  < pivot | == pivot | > pivot  in one pass (the Dutch
// national flag of tutorial 01.02). The middle part is final, so many equal keys make the sort faster. Not stable.
template <class T>
void quickSort3(std::vector<T>& a, int lo, int hi, std::mt19937& rng) {
    while (hi - lo > 1) {
        const T pivot = a[lo + (int)(rng() % (hi - lo))];
        int lt = lo, i = lo, gt = hi;                           // a[lo..lt) < pivot, a[lt..i) == pivot, a[gt..hi) > pivot
        while (i < gt) {
            if (a[i] < pivot) std::swap(a[lt++], a[i++]);
            else if (pivot < a[i]) std::swap(a[i], a[--gt]);
            else ++i;
        }
        if (lt - lo < hi - gt) { quickSort3(a, lo, lt, rng); lo = gt; }
        else { quickSort3(a, gt, hi, rng); hi = lt; }
    }
}
```

One pass on `[3, 1, 3, 2, 3, 5, 3]` with pivot value 3 (`lt`, `i`, `gt` as in the comments):

| looks at | action | array after | lt | i | gt |
|---|---|---|---|---|---|
| 3 | equal, move on | 3 1 3 2 3 5 3 | 0 | 1 | 7 |
| 1 | smaller, swap with `a[lt]` | 1 3 3 2 3 5 3 | 1 | 2 | 7 |
| 3 | equal | 1 3 3 2 3 5 3 | 1 | 3 | 7 |
| 2 | smaller | 1 2 3 3 3 5 3 | 2 | 4 | 7 |
| 3 | equal | 1 2 3 3 3 5 3 | 2 | 5 | 7 |
| 5 | larger, swap with `a[gt - 1]` | 1 2 3 3 3 3 5 | 2 | 5 | 6 |
| 3 | equal | 1 2 3 3 3 3 5 | 2 | 6 | 6 |

The result is `[1, 2 | 3, 3, 3, 3 | 5]`: only `[1, 2]` and `[5]` are sorted further. An array of equal keys is finished in one pass, 2n comparisons (2,000 for n = 1000).

### 4.6 The stack

Recursing into both parts can go `n` frames deep on a bad input and overflow the stack. Recursing into the smaller part and looping on the larger one limits the depth: each recursive call gets at most half of the elements, so there are at most ⌊log2 n⌋ nested calls, whatever the pivots. The time can still be quadratic; only the memory is safe.

---

## 5. Heapsort

### 5.1 A heap in an array

Store a binary tree in an array: the children of node `k` are `2k + 1` and `2k + 2`. In a **max-heap** every node is at least as large as its children, so the largest element is at index 0. A heap is not sorted, but it is cheap to repair. (Heaps and priority queues get their own tutorial in the Trees area.)

`siftDown` repairs one node: swap it with its larger child until it is not smaller than both children:

```cpp
// Sift a[lo + i] down inside the max-heap a[lo..lo + n): node k has the children 2k + 1 and 2k + 2 (counted from lo).
template <class T>
void siftDown(std::vector<T>& a, int lo, int n, int i) {
    T x = a[lo + i];
    while (2 * i + 1 < n) {
        int c = 2 * i + 1;
        if (c + 1 < n && a[lo + c] < a[lo + c + 1]) ++c;        // the larger child
        if (!(x < a[lo + c])) break;                            // x is not smaller than its larger child: it stays
        a[lo + i] = a[lo + c];
        i = c;
    }
    a[lo + i] = x;
}
```

### 5.2 The sort

First **build** the heap: call `siftDown` on every node that has a child, from the last one back to the root. Half of the nodes are leaves, a quarter can sink one level, an eighth two levels, and so on, so the whole build costs O(n), not O(n log n). Then **extract** n - 1 times: swap the root (the maximum) with the last element of the heap, shrink the heap by one, and sift the new root down. The sorted part grows from the back:

```cpp
// Heapsort of a[lo..hi): build a max-heap bottom-up in O(n), then n - 1 times move the maximum behind the shrinking heap.
// O(n log n) always, O(1) extra memory, not stable.
template <class T>
void heapSort(std::vector<T>& a, int lo, int hi) {
    int n = hi - lo;
    for (int i = n / 2 - 1; i >= 0; --i) siftDown(a, lo, n, i);
    for (int end = n - 1; end > 0; --end) {
        std::swap(a[lo], a[lo + end]);
        siftDown(a, lo, end, 0);
    }
}
```

On `[4, 10, 3, 5, 1]` (the bar separates the heap from the sorted tail):

| step | array |
|---|---|
| after the build | 10 5 3 4 1 |
| extract with `end = 4` | 5 4 3 1 \| 10 |
| extract with `end = 3` | 4 1 3 \| 5 10 |
| extract with `end = 2` | 3 1 \| 4 5 10 |
| extract with `end = 1` | 1 \| 3 4 5 10 |

Heapsort is O(n log n) in every case and uses O(1) memory. It is **not stable**, and it is slow in practice (section 9.4): sifting jumps between a parent and a child far away in the array, which fits a poor use of the cache. Its value is the guarantee: no bad input, no buffer, no recursion.

---

## 6. How fast can a comparison sort be?

### 6.1 The lower bound

A sort that only compares elements can be drawn as a binary tree: each internal node is one comparison, each leaf is one answer, "the sorted order is this permutation". For `n` distinct elements there are `n!` orders, and every one must be a leaf (otherwise two different inputs would get the same answer). A binary tree with `n!` leaves has a path with at least ⌈log2 n!⌉ comparisons, so **some input costs at least ⌈log2 n!⌉ comparisons** whatever the algorithm. And `log2 n! ≈ n log2 n - 1.44 n`, so every comparison sort needs on the order of n log n comparisons.

| n | ⌈log2 n!⌉ | merge sort worst case | ⌈n log2 n⌉ |
|---|---|---|---|
| 3 | 3 | 3 | 5 |
| 5 | 7 | 8 | 12 |
| 10 | 22 | 25 | 34 |
| 100 | 525 | 573 | 665 |
| 1000 | 8530 | 8977 | 9966 |

Merge sort is within about 5% of the best any comparison sort can do. The bound says nothing about sorts that do not compare (section 7).

### 6.2 The numbers side by side

Comparisons made by the sorts of this tutorial on `n = 1000` values (random: `mt19937` with seed 2024, values below 10^6; sorted; all equal). These are counts, so they do not depend on the machine:

| sort | random | sorted | all equal |
|---|---|---|---|
| insertion | 256,592 | 999 | 999 |
| merge | 8,699 | 4,932 | 4,932 |
| quicksort, Lomuto, last pivot | 10,725 | 499,500 | 499,500 |
| quicksort, Hoare, middle pivot | 15,354 | 10,975 | 11,862 |
| quicksort, three-way, random pivot | 18,119 | 16,671 | 2,000 |
| heapsort | 16,849 | 17,583 | 2,994 |
| introsort (section 9) | 11,085 | 7,251 | 7,274 |

Insertion sort is quadratic except on sorted input. Merge sort on random input (8,699) is 2% above the lower bound. Lomuto's quicksort makes 23% more comparisons than merge sort on random input and collapses on sorted and equal input. Hoare's makes more than Lomuto's on random input (its pivot is not placed for good and is compared again) but never collapses. The three-way partition pays two comparisons per element but finishes equal keys in one pass. Heapsort is steady and higher. Introsort never collapses: its three counts stay between 7,251 and 11,085.

---

## 7. Sorting without comparing

The n log n bound counts comparisons. If the keys have structure, there are sorts that never compare two keys.

### 7.1 Counting sort

Keys are integers in `[0, k)`. Count the elements of each key, turn the counts into *first slots* with a running sum, then place the elements left to right, each at the next free slot of its key. For the records `2a, 0b, 2c, 3d, 0e, 1f` (number = key, letter = original position):

| key | 0 | 1 | 2 | 3 |
|---|---|---|---|---|
| count | 2 | 1 | 2 | 1 |
| first slot | 0 | 2 | 3 | 5 |

`2a` goes to slot 3, `0b` to 0, `2c` to 4 (the next slot of key 2), `3d` to 5, `0e` to 1, `1f` to 2: the output `0b 0e 1f 2a 2c 3d` is sorted and stable.

```cpp
// Stable counting sort of a by key(x), where every key is in [0, k). O(n + k) time, O(n + k) extra memory.
template <class T, class Key>
void countingSortBy(std::vector<T>& a, int k, Key key) {
    std::vector<int> next(k + 1, 0);
    for (const T& x : a) ++next[key(x) + 1];                    // next[v + 1] = how many elements have key v
    for (int v = 0; v < k; ++v) next[v + 1] += next[v];         // next[v] = the first slot of key v
    std::vector<T> out(a.size());
    for (const T& x : a) out[next[key(x)]++] = x;               // left to right: equal keys keep their order
    a = out;
}
```

Time and extra memory are O(n + k). It wins when `k` is not much larger than `n` (ages, grades, byte values) and is useless for 64-bit keys.

### 7.2 Radix sort

For large keys, sort by one digit at a time with a stable counting sort, **least significant digit first**. After the tens pass the numbers are ordered by tens, and because the pass is stable, numbers with equal tens digits stay in units order; and so on. For `170 45 75 90 802 24 2 66`:

| after the pass on | array |
|---|---|
| units | 170 90 802 2 24 45 75 66 |
| tens | 802 2 24 45 66 170 75 90 |
| hundreds | 2 24 45 66 75 90 170 802 |

```cpp
// LSD radix sort of non-negative ints in base 10: one stable counting sort per digit, least significant digit first.
void radixSort(Vec& a) {
    int mx = a.empty() ? 0 : *std::max_element(a.begin(), a.end());
    for (long long place = 1; mx / place > 0; place *= 10)
        countingSortBy(a, 10, [&](int x) { return (int)(x / place % 10); });
}
```

Here `Vec` is `std::vector<int>`. With `d` digits in base `b` the cost is O(d (n + b)). This base-10 version is for reading; real code uses a power of two, so a digit is a shift and a mask: a 32-bit key in base 256 is 4 passes of 256 counters. Negative numbers need an offset or a flipped sign bit.

### 7.3 Bucket sort

If the keys are spread evenly over a known range, drop each into one of `n` equal buckets, sort each bucket (about one key each) and read the buckets in order: O(n) on average, as bad as the inner sort if every key lands in one bucket. The same idea solves LeetCode 164 (Problem 3): among `n` numbers the largest gap is at least `⌈(max - min) / (n - 1)⌉`, so with buckets of width `max(1, ⌊(max - min) / (n - 1)⌋)` it is never inside a bucket, and only each bucket's minimum and maximum matter.

**When to use which:** small integers or characters: counting sort; fixed-width integers and huge `n`: radix sort (measure it against `std::sort`, section 9.4); uniformly spread reals: bucket sort; anything else (objects, strings, a comparator): a comparison sort.

---

## 8. Stability

The standard use is sorting by two keys: **sort by the secondary key first, then by the primary key with a stable sort**; equal primary keys keep the secondary order. This is how Python's sorting guide sorts by grade with ties ordered by age: sort by age, then by grade (it states that Python's sorts are guaranteed stable). A single comparator on both keys needs no stability.

| Stable | Not stable |
|---|---|
| insertion sort, merge sort, counting sort, LSD radix sort (every pass must be stable), `std::stable_sort` | quicksort (all three versions here), heapsort, selection sort, `std::sort` |

On the smallest case, two records with equal keys and ids 0 and 1, the companion file shows insertion sort, merge sort and counting sort leaving `0 1`, and Lomuto's quicksort, Hoare's quicksort and heapsort returning `1 0`. Stability is a property of the algorithm, not of one input: an unstable sort can keep the order of equal keys on a lucky input (Lomuto's quicksort does on the keys `1, 1, 0`: its first partition puts the two equal keys in the wrong order and the second puts them back).

To make any sort stable, sort pairs `(key, original index)` and compare the index when the keys are equal. Two cautions from real systems (section 9.3): GNU `sort -k` breaks ties by comparing the whole line, so equal keys do **not** keep their input order unless you pass `-s`; and programs that relied on glibc's `qsort` being a stable merge sort, although the C standard does not promise it, were the reason a change to an unstable introsort was reverted in January 2024.

---

## 9. Introsort, and what real libraries do

### 9.1 Putting the three together

Quicksort is fast on typical input but can go quadratic; heapsort never does but is slow; insertion sort is the best on a handful of elements. **Introsort** takes the strength of each:

- run quicksort with the median-of-three pivot;
- count the depth; past `2 · ⌊log2 n⌋` levels the pivots have been bad, so finish that part with heapsort (the worst case stays O(n log n));
- stop partitioning at parts of at most 16 elements, and sort the whole array with one insertion sort at the end: every element is within 16 places of its final position, so that pass is linear.

```cpp
// Order a[lo], a[mid], a[hi - 1] so that a[mid] holds their median: a pivot that sorted or reversed input cannot ruin.
template <class T>
void medianOfThree(std::vector<T>& a, int lo, int hi) {
    int mid = lo + (hi - lo - 1) / 2;
    if (a[mid] < a[lo]) std::swap(a[mid], a[lo]);
    if (a[hi - 1] < a[mid]) std::swap(a[hi - 1], a[mid]);
    if (a[mid] < a[lo]) std::swap(a[mid], a[lo]);
}

// Quicksort rounds on a[lo..hi) until every part has at most 16 elements. When `depth` rounds have not been enough (a bad
// run of pivots), the part is finished by heapsort instead, so the worst case stays O(n log n).
template <class T>
void introSortLoop(std::vector<T>& a, int lo, int hi, int depth) {
    while (hi - lo > 16) {
        if (depth == 0) { heapSort(a, lo, hi); return; }
        --depth;
        medianOfThree(a, lo, hi);
        int j = partitionHoare(a, lo, hi);
        introSortLoop(a, j + 1, hi, depth);
        hi = j + 1;
    }
}

// Introsort: quicksort with median-of-three pivots and a depth limit of 2 * floor(log2 n), then one insertion sort over the
// whole array. Every element is within 16 places of its final one, so that last pass is linear. Not stable.
template <class T>
void introSort(std::vector<T>& a) {
    int n = (int)a.size();
    introSortLoop(a, 0, n, 2 * (std::bit_width((unsigned)n) - 1));
    insertionSort(a, 0, n);
}
```

This is the "introsort" row of section 6.2. The C++ standard requires `std::sort` to make O(n log n) comparisons in the worst case (cppreference; a defect report applied it to C++98 too), which is why implementations moved from plain quicksort to introsort.

### 9.2 What libstdc++ does

I read `std::sort` in GCC 13.3 (`bits/stl_algo.h`). It is the structure above: depth limit `2 * __lg(n)`, `_S_threshold = 16`, the pivot is the median of the second, middle and last element moved to the front, a Hoare-style `__unguarded_partition` (two scans, no bounds checks), `__partial_sort` (heapsort) when the depth is used up, and a final insertion sort pass (`__final_insertion_sort`). libc++ is also an introsort: its source (`algorithm.cpp`) computes the depth limit as `2 * __bit_log2(n)`, and a comment says the "bitset" partition is used only for arithmetic types. Neither is stable.

### 9.3 Other systems (checked against their documentation or source)

| System | Sort | Note |
|---|---|---|
| Linux kernel `sort()` (`lib/sort.c`, v5.8.1) | heapsort | the header: fast, small, non-recursive, guaranteed O(n log n) |
| Linux kernel `list_sort()` | merge sort on a linked list | documented as stable |
| glibc `qsort` | merge sort, heapsort when memory runs out | a change to introsort was reverted in January 2024: programs relied on stability (LWN) |
| Python `sorted` and `list.sort` | Timsort | stable by guarantee |
| Java `Arrays.sort(int[])` | dual-pivot quicksort | |
| Java `Arrays.sort(Object[])` | stable merge sort adapted from Timsort | |
| GNU `sort` | external merge sort, section 10 | |

In these examples, a function that must not fail on any input and must use little memory chooses heapsort or introsort; one that sorts objects and promises stability chooses a merge sort.

### 9.4 Measured on this machine

10^6 random ints below 10^9, best of 3 runs, in milliseconds, on a 2-core virtual machine with `-O2`. The companion file prints these with `./demo --time`.

| sort | g++ 13.3 | clang++ 18 |
|---|---|---|
| `std::sort` | 80 | 75 |
| `introSort` (section 9.1) | 80 | 85 |
| `mergeSort` | 125 | 85 |
| `heapSort` | 165 | 180 |
| `radixSort` (base 10) | 195 | 85 |

Your numbers will differ. Two things hold on both compilers: heapsort is about twice as slow as introsort although it makes only 1.5 times the comparisons (16,849 against 11,085 at n = 1000), which fits its poor use of the cache; and my introsort is within about 15% of `std::sort`. The other rows move between the compilers (`mergeSort` 125 ms against 85, `radixSort` 195 against 85; I did not look into why). The honest answer to "which is fastest" is: measure on your compiler and your data.

---

## 10. External merge sort

When the data does not fit in memory, sort pieces and merge them:

1. **Make runs.** Read as much as fits in memory (`M`), sort it with any in-memory sort, write it out as a *run*: `⌈N / M⌉` runs.
2. **Merge.** Merge `k` runs at once, with one buffer per run and one for the output, picking the smallest head with a min-heap of `k` entries (O(log k) per element). If more than `k` runs remain, the merged runs go through another pass.

Every pass reads and writes all the data, and the disk is the bottleneck, so the cost is the number of passes, `1 + ⌈log_k (N / M)⌉`. Arithmetic, not a measurement: `N = 64 GB`, `M = 4 GB` gives 16 runs. Two-way merging takes 4 merge passes, 5 in all, 640 GB read and written; a 16-way merge takes 1 merge pass, 2 in all, 256 GB. For 1 TB (1024 GB) there are 256 runs, and a 16-way merge needs 2 merge passes, 3 in all, 6,144 GB. The limit on `k` is memory for the `k + 1` buffers.

GNU `sort` works this way: with more inputs than its batch size it merges them in groups through temporary files; `--batch-size` sets the group size (default 16; the manual says it may change) and `-S` the initial memory buffer. The heap of `k` heads is the structure behind the interview problem "merge k sorted lists".

---

## 11. Mistakes that cost points

1. **Hoare's partition with the last element as the pivot.** It can return the whole range, and the sort never ends (Problem B2).
2. **A tie sent to the right in a merge.** The code still sorts but loses stability (Problem B1).
3. **A radix loop that stops one pass early** (`mx / place > 1`): `[100, 20]` stays unsorted (Problem B3).
4. **Lomuto on many equal keys.** Quadratic; use the three-way partition.
5. **Recursing into both parts.** The stack can be `n` frames deep; recurse into the smaller one.
6. **Saying "quicksort is O(n log n)".** Say "on average; the worst case is O(n²), avoided by a random or median-of-three pivot, or introsort".
7. **Expecting `std::sort` to keep equal elements in order.** It does not; use `std::stable_sort`. And never give it a comparator that is not a strict ordering (`<=`): undefined behaviour (tutorial 05).

---

## 12. Questions you will be asked

**Merge sort or quicksort?** Merge sort: stable, O(n log n) always, needs O(n) memory, the choice for linked lists and for sorting from disk. Quicksort: in place, usually faster on arrays, quadratic in the worst case unless the pivot or introsort protects it.

**Why is quicksort usually faster in practice if both are n log n?** It works in place with sequential scans, which suits the cache. The measurement in 9.4 shows a gap that depends on the compiler, so say "usually".

**What is the worst case of quicksort and how do you avoid it?** O(n²) when the pivots split badly (sorted input with a fixed end pivot, many equal keys). Random or median-of-three pivot, the three-way partition, and heapsort when the depth is too large (introsort).

**What does `std::sort` do?** Introsort: quicksort with median-of-three, heapsort when too deep, insertion sort for the last 16 elements. O(n log n) worst case, not stable.

**Why can't a comparison sort beat n log n?** A decision tree with `n!` leaves has depth at least `log2 n!`, about `n log2 n - 1.44 n`.

**Then how does counting sort do O(n)?** It never compares keys; it uses them as indexes, which needs a small range `k`: O(n + k).

**What is stability and when does it matter?** Equal keys keep their input order. It matters when sorting by several keys one after another.

**Sort a 100 GB file with 4 GB of memory.** External merge sort: sort 4 GB runs, then merge them k at a time with a heap; count the passes.

---

## 13. Problems

**How to use this section.** Try each problem for 15 minutes (easy) or 30 minutes (harder) before reading anything. If you are stuck, read only the **How** lines, then try again, and only then open the answer at the bottom. Log each attempt in the [practice log](../practice_log.md).

### On paper

**Problem A: Trace.** Run `mergeSort` on `[7, 3, 5, 3, 1]`: list the merges in order with the number of comparisons for each, and say which of the two 3s ends up first.
*Where:* paper. *How:* the splits use `mid = lo + (hi - lo) / 2`; use the table of 3.1 for each merge.

**Problem B: Find the bug.** Each function should do what its comment says. For each, give an input that breaks it and say what happens.

```cpp
// 1. in mergeRuns, which must stay stable, the first loop became:
while (i < mid && j < hi) tmp[k++] = (a[i] < a[j]) ? a[i++] : a[j++];

// 2. in partitionHoare, whose two parts must both be non-empty, the pivot line became:
const T pivot = a[hi - 1];

// 3. in radixSort, which sorts non-negative ints, the loop head became:
for (long long place = 1; mx / place > 1; place *= 10)
```
*Where:* paper. *How:* try each on a tiny input; compare with sections 3.2, 4.3 and 7.2.

**Problem C: Which sort?** Name the best choice and say why: (1) 10^6 integers between 0 and 255; (2) 20 records that are almost sorted; (3) a linked list of 10^6 nodes, O(1) extra memory; (4) 10^7 records by grade, equal grades in input order; (5) 500 GB on disk; (6) a kernel function that must not recurse and must never be slow on a bad input.
*Where:* paper. *How:* ask for each: how big, what keys, is stability needed, is memory limited.

**Problem D: The limit.** What is the smallest number of comparisons that could possibly sort 6 distinct numbers in the worst case? Could an algorithm that always uses 8 comparisons on 6 numbers be correct?
*Where:* paper. *How:* section 6.1; `6! = 720`.

**Problem E: Passes.** 200 GB of data, 8 GB of memory. How many runs? How many passes and how many GB read and written with a 10-way merge, and with a 25-way merge?
*Where:* paper. *How:* section 10; a pass reads and writes everything once.

### In code

**Problem 1: Sort an Array** (LeetCode 912, medium)
*Where:* leetcode.com/problems/sort-an-array. *Goal:* sort without a library sort, in O(n log n) time. Write it three times (merge sort, quicksort with a random pivot, heapsort) and compare the time.
*How:* sections 3, 4 and 5. Watch the sorted input and the input of equal values.

**Problem 2: Sort List** (LeetCode 148, medium)
*Where:* leetcode.com/problems/sort-list. *Goal:* O(n log n) time; then O(1) extra memory.
*How:* section 3.4. Split with slow and fast pointers, merge by relinking; the bottom-up version for O(1).

**Problem 3: Maximum Gap** (LeetCode 164, medium)
*Where:* leetcode.com/problems/maximum-gap. *Goal:* linear time and linear extra space.
*How:* section 7.3 (buckets of width `max(1, ⌊(max - min) / (n - 1)⌋)`), or radix sort and a scan.

**Problem 4: Count of Smaller Numbers After Self** (LeetCode 315, hard)
*Where:* leetcode.com/problems/count-of-smaller-numbers-after-self. *Goal:* for each element, the number of smaller elements to its right, in O(n log n).
*How:* section 3.5. Sort indexes instead of values; when a left element is placed, add the number of right elements already placed (they are smaller) to its count.

**Problem 5: Reverse Pairs** (LeetCode 493, hard)
*Where:* leetcode.com/problems/reverse-pairs. *Goal:* the number of pairs `i < j` with `nums[i] > 2 * nums[j]`, in O(n log n).
*How:* section 3.5, but count the pairs in a separate two-pointer pass over the two sorted halves before merging; the condition is not the merge order. Watch the overflow of `2 * nums[j]`.

**Problem 6: Radix sort** (your own editor)
*Where:* your editor. *Goal:* LSD radix sort for 32-bit unsigned ints in base 256, then for signed ints; compare with `std::sort` on 10^6 values.
*How:* section 7.2. Digit `(x >> shift) & 255`; flip the sign bit for signed values.

**Problem 7: External sort** (your own editor)
*Where:* your editor. *Goal:* sort a text file of integers while holding at most 10^5 of them: write sorted runs to temporary files, then a k-way merge with `std::priority_queue`.
*How:* section 10.

**More practice:** Sort Colors (75, the three-way partition), Merge Sorted Array (88), Merge Two Sorted Lists (21), Merge k Sorted Lists (23), Sort Characters By Frequency (451); CSES "Sorting and Searching".

---

## Answers

### Problem A

The splits are `[7, 3] | [5, 3, 1]`, then `[5] | [3, 1]`. The merges, in order: `[7]+[3]` gives `[3, 7]` (1 comparison); `[3]+[1]` gives `[1, 3]` (1); `[5]+[1, 3]` gives `[1, 3, 5]` (2); and `[3, 7]+[1, 3, 5]` gives `[1, 3, 3, 5, 7]` (4), in which the tie of the two 3s takes the left one. Eight comparisons in all. The 3 from `[3, 7]` (the first 3 of the input) comes first.

### Problem B

1. The changed loop takes `a[j]` when the heads are equal (`a[i] < a[j]` is false), so for the records `(1, id 0)` and `(1, id 1)` with `mid = 1` it outputs id 1 before id 0. Fix: `a[j] < a[i] ? a[j++] : a[i++]`.
2. With the last element as the pivot, on `[1, 2]` the scans stop at `i = 1` and `j = 1`, so the partition returns `j = 1`, and the "left part" `[0, 2)` is the whole range: the sort would keep working on the same range forever. The pivot must not be the last element; the middle one works.
3. The loop stops when `mx / place` is 1 instead of 0, so it skips the pass on the leading digit when that digit is 1. On `[100, 20]` the units and tens passes keep the order and the hundreds pass never runs: the result is `[100, 20]`.

### Problem C

1. Counting sort with `k = 256`: O(n + 256), no comparisons. 2. Insertion sort: close to linear on nearly sorted input, no overhead. 3. Bottom-up merge sort that relinks nodes. 4. A stable sort (`std::stable_sort`), or counting sort by grade if there are few grades. 5. External merge sort. 6. Heapsort: in place, no recursion, O(n log n) in the worst case (this is what the kernel's `sort()` does).

### Problem D

`⌈log2 720⌉ = 10`, because `2^9 = 512 < 720 <= 1024 = 2^10`. Some input needs at least 10 comparisons, so an algorithm that always makes 8 comparisons on 6 numbers cannot be correct: it can tell apart at most `2^8 = 256` orders.

### Problem E

`⌈200 / 8⌉ = 25` runs. With a 10-way merge: `⌈log10 25⌉ = 2` merge passes, 3 passes in all, `2 × 200 × 3 = 1,200 GB` read and written. With a 25-way merge: 1 merge pass, 2 passes in all, 800 GB.

### Problems 1 to 7

1. Merge sort with one buffer allocated once; quicksort with a random pivot and the three-way partition; heapsort as in section 5.
2. Find the middle with slow and fast pointers, cut, sort both halves, merge by relinking. For O(1) memory merge runs of width 1, 2, 4 from the front.
3. Bucket width `max(1, ⌊(max - min) / (n - 1)⌋)`; keep each bucket's minimum and maximum; the answer is the largest gap between a bucket's minimum and the maximum of the previous non-empty bucket.
4. Merge sort on indexes; when a left element is placed, add the number of right elements already placed to its count.
5. Merge sort; before each merge count the pairs with one pointer over each sorted half (`nums[i] > 2 * nums[j]`), in 64-bit arithmetic.
6. For each of 4 passes: count `(x >> shift) & 255`, prefix sums, place left to right. For signed ints XOR the sign bit before sorting and after.
7. Read `10^5` numbers, sort, write `run_i.txt`; then a min-heap of (value, run index) pops the smallest and refills from the same run.

---

## Cheat sheet

| Sort | Best | Average | Worst | Extra memory | Stable |
|---|---|---|---|---|---|
| insertion | O(n) | O(n²) | O(n²) | O(1) | yes |
| merge | O(n log n) | O(n log n) | O(n log n) | O(n), O(1) on a linked list | yes |
| quick (random or median-of-three pivot) | O(n log n) | O(n log n) | O(n²) | O(log n) stack | no |
| heap | O(n log n) | O(n log n) | O(n log n) | O(1) | no |
| introsort (`std::sort`) | O(n log n) | O(n log n) | O(n log n) | O(log n) stack | no |
| counting | O(n + k) | O(n + k) | O(n + k) | O(n + k) | yes |
| radix (LSD, `d` digits of base `b`) | O(d (n + b)) | O(d (n + b)) | O(d (n + b)) | O(n + b) | yes |

- **Lower bound:** ⌈log2 n!⌉ ≈ n log2 n - 1.44 n comparisons in the worst case.
- **Merge:** left element first on a tie; count inversions with `mid - i` when a right element goes first.
- **Quicksort:** recurse into the smaller part; random or median-of-three pivot; three-way partition for equal keys; Hoare's pivot is never the last element.
- **Introsort:** quicksort, heapsort after `2 ⌊log2 n⌋` levels, insertion sort for parts of at most 16.
- **Counting and radix:** stable passes, least significant digit first.
- **External sort:** memory-sized runs, k-way merge with a heap, `1 + ⌈log_k (N / M)⌉` passes.

**Sources** (checked while writing this tutorial):
- LeetCode statements: [912](https://hackmd.io/@Marsgoat/leetcode912), [148](https://leetcode.doocs.org/en/lc/148/) and [164](https://leetcode.doocs.org/en/lc/164/), [493](https://hao-fu-1.gitbook.io/oj/binary_tree_and_divide_conquer/315.-count-of-smaller-numbers-after-self/493.-reverse-pairs), [315](https://brain-leetcode.pages.dev/03xx/315/) (mirrors and notes pages; difficulty labels only where a page stated them).
- libstdc++: the header `bits/stl_algo.h` of GCC 13.3, read on the machine used to write this tutorial. libc++: [`algorithm.cpp`](https://src.rcs.uwaterloo.ca/source/raw/llvm-project/libcxx/src/algorithm.cpp) (a mirror of the LLVM source). The worst-case requirement for `std::sort`: [cppreference](https://en.cppreference.com/cpp/algorithm/sort).
- glibc `qsort`: [LWN, January 2024](https://lwn.net/Articles/960309). Linux kernel: `lib/sort.c` at [v5.8.1](https://elixir.free-electrons.com/linux/v5.8.1/source/lib/sort.c) and [`lib/list_sort.c`](https://android.googlesource.com/kernel/common/+/52d1e606ee733/lib/list_sort.c).
- Python: the [Sorting HOWTO](https://docs.python.org/3.11/howto/sorting.html). Java: the [source of `java.util.Arrays`](https://code.yawk.at/java/8/java/util/Arrays.java) (a mirror of an older JDK; the version is not stated on the page). GNU sort: the [manual](https://www-zeuthen.desy.de/dv/documentation/unixguide/infohtml/coreutils/sort-invocation.html).

---

## Sources

- Counts, outputs and timings in this tutorial come from the companion `04_sorting_algorithms_inside_demo.cpp` (it prints the number of checks it passed) on the machine described in the text. Times are that machine's, not yours.
- The algorithms and data structures are standard; for further reading see the "Extra practice and resources" section of the [area plan](00_plan.md), which lists the textbook chapters and free references for this area. The explanations and code are my own.
- LeetCode numbers and titles are from memory; check a number before you rely on it.

**Next:** tutorial 05, sorting patterns and comparators (strict weak ordering and what breaks when it is wrong, quickselect and median of medians, top-K by heap versus selection versus bucket, interval sorting). Say "next" when you are ready.
