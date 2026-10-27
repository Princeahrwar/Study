# DSA 03.01: Binary Search Templates

*Part of the [Binary search and sorting plan](00_plan.md) · Time: about 5 hours (2 reading, 3 problems) · You need: [Arrays 02, two pointers](../01_arrays_and_strings/02_two_pointers.md) (why a move never skips the answer) · Companion code: [`01_binary_search_templates_demo.cpp`](01_binary_search_templates_demo.cpp)*

By the end of this tutorial you can:

- write binary search from one template, state its loop invariant, and say why it terminates and why it has no off-by-one;
- turn any "find the boundary" question into a yes/no predicate and a single call;
- write `lower_bound` and `upper_bound`, and build contains, count, first and last position, insertion point, floor and ceiling from them;
- spot the classic bugs on sight: a middle that overflows, a middle that rounds the wrong way, a bound that cannot return "none";
- explain what the search returns when the predicate is not monotone, and why that is the key to peak finding;
- say why a binary search is slow on a big array, and what a branch-free version changes.

The functions are in the companion file. It checks every one of them against the standard library and a brute-force scan on every sorted array of length up to 6 over four values, on random arrays, on all 2047 predicates up to length 10, and at the extremes of `int32_t`, and it prints every number quoted below. It compiles with `-Wall -Wextra -fsanitize=address,undefined`. The timings in section 7 are from one machine.

---

## 1. Why a ten-line function is hard

Binary search has a bad record. Joshua Bloch's 2006 post [Nearly All Binary Searches and Mergesorts are Broken](https://research.google/blog/extra-extra-read-all-about-it-nearly-all-binary-searches-and-mergesorts-are-broken/) says, citing Jon Bentley's *Programming Pearls*, that the first binary search was published in 1946 and the first one correct for all array sizes in 1962. In Bloch's account, Bentley had incoming PhD students write one in class and most of them, Bloch included, got it wrong.

Bloch's own bug was in the Java library: `int mid = (low + high) / 2;`. The sum overflows once `low + high` passes 2^31 - 1, and the middle becomes negative. The same line was in Bentley's book, and by Bloch's account it was in the JDK for about nine years before it was reported. His lesson: "carefully consider the invariants in your programs".

That is the plan here. There is **one template**, with an invariant you can state in one sentence, and the demo tries it on every small case.

---

## 2. The idea and the cost

Look at the middle of a sorted array. If it is too small for what you want, so is everything to its left; if it is big enough, you can forget everything to its right. One comparison discards half of the candidates, and halving `n` candidates takes about log2(n) steps. In the worst case the number of probes is ⌊log2 n⌋ + 1:

| n | 1 | 2 | 3 | 4 | 7 | 8 | 9 | 1000 | 1 000 000 | 2^31 - 1 |
|---|---|---|---|---|---|---|---|---|---|---|
| worst case | 1 | 2 | 2 | 3 | 3 | 4 | 4 | 10 | 20 | 31 |

(The demo tries every possible answer for each `n` up to 2000, and every seventh answer for 10^6.) A billion elements need 30 probes.

The array is not what makes this work. What makes it work is a **yes/no question whose answers run no, no, no, yes, yes, yes** along the range. In a sorted array, "is `a[i]` at least 8?" has that shape. So does "is version `i` bad?" when one release broke things, and "is `r × r` bigger than `x`?" when you want a square root. Binary search finds the place where the answers flip.

---

## 3. One template: the first index where a predicate is true

### 3.1 The template

Take the array `a = [1, 3, 3, 5, 8, 8, 8, 10]` and the question "is `a[i] >= 8`?":

```
index:   0  1  2  3  4  5  6  7
a[i]:    1  3  3  5  8  8  8  10
a[i]>=8: F  F  F  F  T  T  T  T      the boundary is index 4
```

The template finds that boundary in a half-open range `[lo, hi)`. It returns `hi` when every answer is "no":

```cpp
// The first index in [lo, hi) where pred is true, or hi if there is none.
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

Three facts make it correct, and each takes one line to check.

1. **The invariant.** Once `lo` has moved, the answer at `lo - 1` is "no"; once `hi` has moved, the answer at `hi` is "yes". At the start neither has moved, and there is nothing to claim. `lo = mid + 1` happens only after a "no" at `mid`, which becomes `lo - 1`. `hi = mid` happens only after a "yes" at `mid`, which becomes the new `hi`.
2. **Progress.** `mid` is at least `lo` and below `hi`. So `hi = mid` makes the range smaller, and `lo = mid + 1` makes it smaller. The loop cannot run forever.
3. **The exit.** When `lo == hi`, "no" is just below and "yes" is right there. If the answers are monotone, that is *the* boundary.

### 3.2 A trace

The search for the first `a[i] >= 8` in the array above:

| step | lo | hi | mid | a[mid] | a[mid] >= 8 | then |
|---|---|---|---|---|---|---|
| 1 | 0 | 8 | 4 | 8 | yes | `hi = 4` |
| 2 | 0 | 4 | 2 | 3 | no | `lo = 3` |
| 3 | 3 | 4 | 3 | 5 | no | `lo = 4` |
| end | 4 | 4 | | | | return 4 |

Three probes for eight elements. The demo prints the same sequence of probes: `4:T 2:F 3:F -> 4`.

### 3.3 Why each choice is what it is

Every line of the template has a reason. The first five changes below were each tried on the demo's code, and its tests caught every one:

| If you change | What goes wrong |
|---|---|
| `lo < hi` to `lo <= hi` | when `lo == hi` the loop asks about index `hi`, which may be outside the range |
| `mid` rounds up | `mid` can equal `hi`: again outside the range |
| `lo = mid` instead of `mid + 1` | when `mid == lo` nothing changes: an endless loop |
| `hi = mid - 1` | throws away a "yes", which may be the answer: the result is one too far |
| start with `hi = n - 1` | the result `n`, meaning "none", can never be returned |
| a pred that is not monotone | see section 6 |

---

## 4. `lower_bound`, `upper_bound` and what you build from them

### 4.1 The two bounds

On a sorted array with duplicates, two questions cover everything:

- `lowerBound(x)`: the first index with `a[i] >= x`, the start of the block of `x`s;
- `upperBound(x)`: the first index with `a[i] > x`, the index just after that block.

```cpp
// The two bounds of a sorted array, and what you can build from them.
size_t lowerBound(const std::vector<int>& a, int x) { return firstTrue<size_t>(0, a.size(), [&](size_t i) { return a[i] >= x; }); }

size_t upperBound(const std::vector<int>& a, int x) { return firstTrue<size_t>(0, a.size(), [&](size_t i) { return a[i] > x; }); }
```

For `x = 8` in `[1, 3, 3, 5, 8, 8, 8, 10]` the first call is the trace of 3.2 and returns 4. The second asks "is `a[i] > 8`?" and probes `4:F 6:F 7:T`, so it returns 7. The block of 8s is the half-open range `[4, 7)`. Both are the same template with a different predicate.

### 4.2 Everything else is arithmetic on the two numbers

| Question | Answer |
|---|---|
| is `x` present? | `lowerBound < n` and `a[lowerBound] == x` |
| how many `x`? | `upperBound - lowerBound` |
| first and last position of `x` | `lowerBound` and `upperBound - 1` (if present) |
| where to insert `x`, before or after equal keys | `lowerBound` or `upperBound` |
| how many elements are smaller than `x`, or at most `x` | `lowerBound` and `upperBound` |
| largest element at most `x` (floor) | index `upperBound - 1`, none if `upperBound == 0` |
| smallest element at least `x` (ceiling) | index `lowerBound`, none if `lowerBound == n` |

```cpp
bool contains(const std::vector<int>& a, int x) { size_t i = lowerBound(a, x); return i < a.size() && a[i] == x; }

size_t countOf(const std::vector<int>& a, int x) { return upperBound(a, x) - lowerBound(a, x); }

size_t largestAtMost(const std::vector<int>& a, int x) { size_t u = upperBound(a, x); return u == 0 ? kNone : u - 1; }

size_t smallestAtLeast(const std::vector<int>& a, int x) { size_t l = lowerBound(a, x); return l == a.size() ? kNone : l; }
```

The demo checks them on all 210 sorted arrays of length up to 6 over the values 0 to 3, with every query from -1 to 4 (1260 pairs), against the standard library and a plain scan. On the array of 3.1:

| x | 0 | 1 | 3 | 4 | 8 | 10 | 11 |
|---|---|---|---|---|---|---|---|
| lowerBound | 0 | 0 | 1 | 3 | 4 | 7 | 8 |
| upperBound | 0 | 1 | 3 | 3 | 7 | 8 | 8 |
| count | 0 | 1 | 2 | 0 | 3 | 1 | 0 |
| present | no | yes | yes | no | yes | yes | no |
| floor | none | 1 | 3 | 3 | 8 | 10 | 10 |
| ceiling | 1 | 1 | 3 | 5 | 8 | 10 | none |

Note the empty block for `x = 4`: both bounds are 3, which is where 4 would be inserted.

### 4.3 Searching values, not an array

The "array" can be the numbers themselves. The integer square root of `x` is the largest `r` with `r × r <= x`. The candidates are `0, 1, 2, ...`, the question "is `r × r > x`?" goes no, no, yes, yes, so the first "yes" minus one is the answer:

```cpp
// A search over values, not over an array: the largest r with r * r <= x. The predicate is "r * r > x".
// 46340 * 46340 <= INT_MAX < 46341 * 46341, so the candidates are 0 .. 46340 and 46341 means "none is too big".
int isqrt(int x) { return (int)firstTrue<int64_t>(0, 46341, [&](int64_t r) { return r * r > x; }) - 1; }
```

`r × r` is computed in 64 bits: in `int` it would overflow. The upper end 46341 is there because 46340² fits in an `int` and 46341² does not, so for `x = INT_MAX` no candidate says "yes" and the call returns 46341, giving 46340. The demo checks every `x` up to 20 000 and the values near `INT_MAX`. This is LeetCode 69, and it is the shape of tutorial 02, binary search on the answer.

### 4.4 The same template in the libraries

| Where | Call | What it does |
|---|---|---|
| C++ | `std::lower_bound`, `std::upper_bound`, `std::equal_range` | the two bounds, and both; `std::binary_search` returns only a `bool` |
| C++ | `std::partition_point(first, last, p)` | `firstTrue` of `not p`; the demo checks they agree on 861 range and boundary pairs |
| Python | `bisect.bisect_left`, `bisect.bisect_right` | lower and upper bound |
| Go | `sort.Search(n, f)` | the smallest `i` in `[0, n)` with `f(i)` true, and `n` if there is none: exactly `firstTrue` |
| Java | `Arrays.binarySearch` | the index of *some* match, or `-(insertion point) - 1` |
| C | `bsearch` | a match; which one, among equal keys, is unspecified |

Two details from the C++ reference. The range need only be *partitioned* by the test (all "no" before all "yes"), not fully sorted. And the iterator version makes about log2(N) comparisons but, unless the iterators are random access, a linear number of iterator steps, so for `std::set` and `std::map` call the member `lower_bound`, which walks the tree.

---

## 5. The other templates, and the traps

### 5.1 The closed interval

Many people learn this version first, with the range `[lo, hi]` and a three-way test:

```cpp
// The classic version: look for an exact match in [lo, hi]. It returns some matching index, not necessarily the first.
int binarySearchClosed(const std::vector<int>& a, int x) {
    int lo = 0, hi = (int)a.size() - 1;
    while (lo <= hi) {                                          // invariant: if x is in the array, it is in [lo, hi]
        int mid = lo + (hi - lo) / 2;
        if (a[mid] == x) return mid;
        if (a[mid] < x) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}
```

The invariant is "if `x` is in the array, it is in `[lo, hi]`", the loop runs while that range is not empty, and `mid ± 1` is right because `mid` has been ruled out. It is fine for "is it there?", and it returns *some* match. On our array with `x = 8` it probes index 3, then 5, and returns **5**: not the first match (4), not the last (6). On `[2, 3, 3, 3, 3, 9]` with `x = 3` it returns 2, where the block is `[1, 5)`. For first, last, insertion point, floor or ceiling it needs extra cases, and each is a place for an off-by-one. `firstTrue` needs none.

### 5.2 The last index where something is true

"Last index with `a[i] <= x`" is `upperBound - 1`, and that is the safe way to write it. If you search for it directly, the middle must round **up**:

```cpp
// The last index in [0, n) where pred is true, or -1. pred must be true, ..., true, false, ..., false.
// The middle must round UP here, or the loop can stall.
template <class Pred>
int lastTrue(int n, Pred pred) {
    int lo = -1, hi = n - 1;                                    // invariant: the answer is in [lo, hi]
    while (lo < hi) {
        int mid = lo + (hi - lo + 1) / 2;                       // rounds up: lo < mid <= hi
        if (pred(mid)) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}
```

With the usual rounding down, `lo = mid` can make no progress: for `lo = 2` and `hi = 3` the middle is 2, the test says "yes", `lo` becomes 2 again, and the loop never ends (the demo caps it at 20 steps and checks that it is still at `lo = 2, hi = 3`). The template of section 3 avoids this because it always moves `lo` to `mid + 1`.

### 5.3 The middle

`(lo + hi) / 2` is the bug in Bloch's post. With 32-bit `int`, `lo = 1 500 000 000` and `hi = 2 000 000 000` have a sum of 3 500 000 000, which does not fit: it wraps to -794 967 296, and half of that is -397 483 648, a negative index. The safe form is `lo + (hi - lo) / 2` (for `lo >= 0`, as here):

```cpp
// What (lo + hi) / 2 does with 32-bit ints once the sum passes 2^31 - 1. The sum is done in unsigned arithmetic here
// so that the wrap-around is defined; with plain int it is undefined behaviour, and in practice it wraps the same way.
int32_t midWrapped(int32_t lo, int32_t hi) { return (int32_t)((uint32_t)lo + (uint32_t)hi) / 2; }

int32_t midSafe(int32_t lo, int32_t hi) { return lo + (hi - lo) / 2; }
```

The demo gets 1 750 000 000 from `midSafe` and -397 483 648 from `midWrapped` (that function uses unsigned arithmetic to show the wrap without undefined behaviour). It also runs `firstTrue` over the whole positive `int32_t` range, finding 2 000 000 000 in 31 probes. Other languages do it their own way: Java's fix is `(low + high) >>> 1`, Go's `sort.Search` computes `uint(i+j) >> 1`, and C++20 has `std::midpoint(a, b)`, which rounds toward `a` and does not overflow. The sum can only overflow when `hi` is above 2^30, which for an array means more than a billion elements, but a search over *values* (tutorial 02) reaches those numbers with no array at all.

### 5.4 Unsigned sizes

`v.size()` is unsigned. `int hi = v.size() - 1` is fine for non-empty vectors, but `size_t hi = v.size() - 1` on an empty vector is 2^64 - 1, and the loop walks off the array. The half-open template uses `hi = n` and never subtracts.

### 5.5 In one table

| | half-open `firstTrue` | closed |
|---|---|---|
| range | `[lo, hi)`, `hi` may be `n` | `[lo, hi]` |
| loop | `while (lo < hi)` | `while (lo <= hi)` |
| middle | `lo + (hi - lo) / 2` | same |
| update | `hi = mid` or `lo = mid + 1` | `hi = mid - 1` or `lo = mid + 1` |
| result | `lo` | `mid` on a hit, `-1` otherwise |
| good for | every boundary question | "is it there?" |

---

## 6. When the predicate is not monotone

The invariant of 3.1 never mentions monotonicity. So even if the answers are a jumble, the result `r` has "no" at `r - 1` (or `r` is 0) and "yes" at `r` (or `r` is `n`): it is **a** boundary, not necessarily the first. The demo checks this for all 2047 yes/no patterns of length up to 10. For `[yes, no, yes, no, yes]` the search probes 2 (yes), then 1 (no), and returns 2.

That is the whole idea behind finding a peak in tutorial 03: you need no sorted order, only a question that is "no" on one side of *some* point and "yes" on the other. It is also why feeding the search an unsorted array does not crash it, and gives an answer that means nothing; the C++ reference says the behaviour is undefined when the range is not partitioned by the test.

---

## 7. Why it is slower than log2(n) suggests

"20 probes" says nothing about time. Each probe in a big array reads a different place in memory (the last four or so land in one cache line; the others do not), and each `if` is a branch the CPU has to guess: with random queries it guesses wrong about half the time, and a wrong guess costs a pipeline flush (see [Computer Organization 10, branch prediction](../../04_computer_organization/10_branch_prediction.md) and [13, cache basics](../../04_computer_organization/13_cache_basics.md)).

A branch-free version runs a fixed number of steps and replaces the jump by a select:

```cpp
// The same answer as lowerBound, written so that the loop runs ceil(log2 n) times whatever the data is and needs no branch.
size_t lowerBoundBranchless(const int* a, size_t n, int x) {
    if (n == 0) return 0;
    const int* base = a;                                        // invariant: a[0 .. base - 1] are < x, and the answer is in [base, base + n]
    while (n > 1) {
        size_t half = n / 2;
        base = (base[half] < x) ? base + half : base;           // a select: the compiler may emit a conditional move or a branch
        n -= half;
    }
    return (size_t)(base - a) + (*base < x);
}
```

The invariant: everything before `base` is below `x`, and the answer is in `[base, base + n]`. Each step compares one element and keeps the half that holds the answer (`n` becomes `n - half`, which is at least `half`), until one element is left, and the last line decides between `base` and `base + 1`. It runs ⌈log2 n⌉ times whatever the data, and the demo checks it against `lowerBound` on every array above and on random ones.

**Measured**, in nanoseconds per query: 2^20 random queries (hits and misses) against a sorted array of `int`, the best of three passes, independent queries so the CPU can overlap them. One 2-core 2.1 GHz Xeon VM; both compilers use libstdc++.

| n (array size) | g++ 13.3 -O2: own `firstTrue` | `std::lower_bound` | branch-free | clang++ 18 -O2: own `firstTrue` | `std::lower_bound` | branch-free |
|---|---|---|---|---|---|---|
| 2^10 (4 KiB) | 70 | 69 | 11 | 64 | 14 | 67 |
| 2^16 (256 KiB) | 123 | 123 | 31 | 119 | 46 | 111 |
| 2^22 (16 MiB) | 340 | 345 | 220 | 340 | 283 | 335 |
| 2^25 (128 MiB) | 568 | 595 | 610 | 557 | 658 | 545 |

What the table says:

- **In cache, the branches are the cost.** At 4 KiB the whole array sits in the L1 cache. The branchy loops take about 6 ns per probe (70 ns for 11 probes), the branch-free loop about 1 ns per step (11 ns for 10 steps): six times faster, and the difference is mispredicted branches.
- **The compiler decides, not the source.** With g++ the select compiled to a conditional move (`cmov`) and the standard library's loop stayed branchy. With clang the standard library's loop became branch-free, and my select was turned back into a conditional jump (I read the assembly of both). Each compiler left a branchy version somewhere, so measure, or read the assembly, before claiming that code is branch-free.
- **At 128 MiB everything is memory latency.** The three are within about 20% of each other at 545 to 658 ns, and the branch-free loop loses its edge. [Algorithmica's binary search chapter](https://en.algorithmica.org/hpc/data-structures/binary-search/) gives the reason: a predicted branch lets the CPU start the next load early, a branch-free loop cannot. That chapter then uses prefetching and a different layout (Eytzinger order) to go faster; this tutorial does not.

---

## 8. Where this shows up

- **`git bisect`** is a binary search over commits, with "does the bug show up?" as the predicate. Its documentation says it uses a binary search, and its example prints "675 revisions left to test after this (roughly 10 steps)".
- **The Linux kernel** has a generic `bsearch()` (`lib/bsearch.c`, `include/linux/bsearch.h`), like the one in C: a sorted array, a key, a comparison function.
- **Where you might use it** (fits to the problem, not claims about a product): the first log record at or after a timestamp in a sorted log; a channel, address or threshold looked up in a sorted range table; bisecting a regression across firmware builds the way `git bisect` does; and every "smallest value that is good enough" question of tutorial 02.

**Choosing.**

| You need | Use |
|---|---|
| is `x` in static sorted data | `std::binary_search`, or the lower bound and one comparison |
| position, count, insertion point | `lowerBound` and `upperBound` |
| the boundary of a monotone yes/no question | `firstTrue` |
| the same, over a range of numbers | `firstTrue` on the values (tutorial 02) |
| search in a `std::set` or `std::map` | the member `lower_bound` |
| data that changes all the time | a tree or a hash table: inserting into a sorted array moves O(n) elements |

---

## 9. Mistakes that cost points

1. **Coding before stating the invariant.** Write one comment: what is known about everything below `lo` and everything from `hi`. Then the update lines are forced.
2. **`(lo + hi) / 2`.** Use `lo + (hi - lo) / 2`.
3. **Mixing two templates.** `lo <= hi` with `hi = mid` never ends; `lo < hi` with `hi = mid - 1` skips an answer. Pick one and keep all its parts.
4. **Starting with `hi = n - 1` when "none" is a valid answer.** Insert at the end, no bad version, no element at least `x`: all need the result `n`.
5. **`size_t` and `- 1` on a possibly empty container.** It wraps to 2^64 - 1.
6. **`lo = mid` with the middle rounded down.** It stalls. Round up, or use `mid + 1`.
7. **Returning the first hit when the problem wants the first match.** The closed version returns whichever match it reaches first.
8. **Trusting the answer without checking it.** `lowerBound` can be `n`, or point at a larger element. Test `i < n && a[i] == x`.
9. **A test that disagrees with the order.** A descending array needs the reversed test; a custom comparator must be the one the data was sorted with.
10. **`std::lower_bound` on `std::set`, `std::map` or `std::list` iterators.** About log2(N) comparisons but a linear number of steps: use the member.
11. **Searching an unsorted or non-monotone range and trusting the result.** The search returns a boundary of the answers (section 6), which means nothing here.

---

## 10. Questions you will be asked

**Write binary search. How do you know it is right?** `firstTrue`, with the invariant stated first: "no" just below `lo`, "yes" at `hi`. It terminates because the range shrinks every step, and on exit `lo == hi` is a "no" next to a "yes".

**Why `lo + (hi - lo) / 2`?** Because `lo + hi` can overflow, as in the JDK bug Bloch reported. The difference cannot, and the result is the same.

**How many comparisons at most?** ⌊log2 n⌋ + 1: 20 for a million, 30 for a billion.

**First and last position of `x` in a sorted array with duplicates?** `lowerBound(x)` and `upperBound(x) - 1`, if `lowerBound(x) < n` and `a[lowerBound(x)] == x`. The count is the difference of the two bounds.

**Why not binary search a linked list?** Reaching the middle costs a walk, so the comparisons are O(log n) but the steps are O(n); a skip list or a tree exists for that.

**Can you binary search unsorted data?** Yes, when you have a question that is "no" before some point and "yes" after: the first bad version, a rotated sorted array, a peak. No order is needed, only that.

---

## 11. Problems

**How to use this section.** Try each problem for 15 minutes (easy) or 30 minutes (harder) before reading anything. If you are stuck, read only the **How** lines, then try again, and only then open the answer at the bottom. Log each attempt in the [practice log](../practice_log.md).

### On paper

**Problem A: Trace.** `a = [2, 4, 4, 4, 7, 9, 9, 12, 15]`. Find `lowerBound` and `upperBound` for `x` = 4, 9, 10 and 16, and write the probes (index and yes/no) of `lowerBound(9)`.
*Where:* paper. *How:* the trace of 3.2; the half-open range starts at `[0, 9)`.

**Problem B: Find the bug.** Each function should return what its comment says. For each, give an input that breaks it and say what happens.

```cpp
// 1. first index with a[i] >= x, or n if none
int f1(const std::vector<int>& a, int x) {
    int lo = 0, hi = (int)a.size() - 1;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] < x) lo = mid + 1; else hi = mid;
    }
    return lo;
}

// 2. last index with a[i] <= x, or -1 if none
int f2(const std::vector<int>& a, int x) {
    int lo = -1, hi = (int)a.size() - 1;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] <= x) lo = mid; else hi = mid - 1;
    }
    return lo;
}

// 3. is x in the array?
bool f3(const std::vector<int>& a, int x) {
    int lo = 0, hi = (int)a.size() - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] == x) return true;
        if (a[mid] < x) lo = mid + 1; else hi = mid;
    }
    return false;
}
```
*Where:* paper. *How:* run each by hand on a one-element array, then on a three-element array; compare with section 3.3.

**Problem C: Probes.** The worst-case number of probes of `firstTrue` for n = 1, 2, 100 and 10^9. What is the largest `n` that needs at most 10?
*Where:* paper. *How:* section 2.

**Problem D: Predicates.** Write the predicate and the call for: (1) the first bad version among versions 1 to `n`; (2) the smallest `x` with `x × x >= 50`; (3) how many elements of `[1, 4, 6, 6, 9]` are at most 7; (4) the insertion index of 6 *after* the equal keys in `[1, 4, 6, 6, 9]`.
*Where:* paper. *How:* section 4.2 and the square root of 4.3.

**Problem E: A jumble.** The answers at indices 0 to 4 are `[yes, no, yes, no, yes]`. Trace `firstTrue(0, 5)`. What does it return, is that the first "yes", and what does hold about the result?
*Where:* paper. *How:* section 6.

**Problem F: Reverse order.** `a = [9, 7, 7, 4, 1]` is sorted in descending order. Find the first index with `a[i] <= 7` and the first with `a[i] < 7` using `firstTrue`.
*Where:* paper. *How:* the test must be "no" first and "yes" after, whatever the order of the data.

### In code

**Problem 1: `firstTrue` and a differential test** (own code)
*Where:* your editor. *Goal:* write `firstTrue` from memory, then test it for every length `n` up to 40 and every boundary against `std::partition_point`, and `lowerBound`, `upperBound` against the library on random arrays with many duplicates. Then break it on purpose in each way listed in 3.3 and see which tests fail.
*How:* section 3. The test is the point: if a mutation survives, add a case that kills it.

**Problem 2: Search Insert Position** (LeetCode 35, easy)
*Where:* leetcode.com/problems/search-insert-position. *Goal:* the index of the target, or where it would be inserted, in O(log n).
*How:* one call to the lower bound.

**Problem 3: Find First and Last Position of Element in Sorted Array** (LeetCode 34, medium)
*Where:* leetcode.com/problems/find-first-and-last-position-of-element-in-sorted-array. *Goal:* `[first, last]`, or `[-1, -1]`, in O(log n).
*How:* 4.2; check that the element is really there before using the bounds.

**Problem 4: First Bad Version** (LeetCode 278, easy)
*Where:* leetcode.com/problems/first-bad-version. *Goal:* the first bad version among 1 to `n`, with as few calls to `isBadVersion` as possible.
*How:* the whole problem is a predicate. `n` can be as large as 2^31 - 1, which is the trap of 5.3.

**Problem 5: Find K Closest Elements** (LeetCode 658, medium)
*Where:* leetcode.com/problems/find-k-closest-elements. *Goal:* the `k` elements of a sorted array closest to `x`, ties to the smaller, in O(log(n - k) + k).
*How:* search for the *start* of the window: the candidates are `0` to `n - k`; compare `x - a[i]` with `a[i + k] - x`.

**Problem 6: Successful Pairs of Spells and Potions** (LeetCode 2300, medium)
*Where:* leetcode.com/problems/successful-pairs-of-spells-and-potions. *Goal:* for each spell, how many potions give a product of at least `success`.
*How:* sort the potions once. For a spell `s` the potions that work are those at least ⌈success / s⌉: one lower bound. Use 64-bit arithmetic.

**Problem 7: A branch-free lower bound** (own code)
*Where:* your editor. *Goal:* the function of section 7, tested against `std::lower_bound` on all arrays up to length 70. Compile with `-O2` and look for `cmov` in the assembly of your function (`-S`), then time it against `std::lower_bound` for `n` = 2^10 and 2^25 with random queries.
*How:* section 7. Report your compiler, flags and machine with the numbers.

**More practice:** Binary Search (704), Sqrt(x) (69), Valid Perfect Square (367), Find Smallest Letter Greater Than Target (744), Heaters (475), Find Right Interval (436).

---

## Answers

### Problem A

`lowerBound(4) = 1`, `upperBound(4) = 4`; `lowerBound(9) = 5`, `upperBound(9) = 7`; for 10 both are 7; for 16 both are 9. The probes of `lowerBound(9)` are `4:no 7:yes 6:yes 5:yes`, so the result is 5.

### Problem B

1. `f1` starts with `hi = n - 1`, so `n` can never be returned. For `[1, 2, 3]` and `x = 5` it returns 2 instead of 3. (With an unsigned size it would also break on an empty vector.)
2. `f2` rounds the middle down but sets `lo = mid`. For `[5]` and `x = 7` the first middle is `-1 + 1/2 = -1`, which reads `a[-1]`; and whenever `lo + 1 == hi` and the test is "yes" it stalls forever.
3. `f3` sets `hi = mid` in the closed version. For `[5]` and `x = 3`: `lo = hi = mid = 0`, the test says go left, `hi` stays 0, and the loop never ends.

### Problem C

1, 2, 7 and 30. At most 10 probes means ⌊log2 n⌋ + 1 <= 10, which is `n <= 1023`.

### Problem D

1. `firstTrue<long long>(1, n + 1, isBad)`: the result `n + 1` would mean "none bad".
2. `firstTrue<int>(0, 51, [](int x) { return x * x >= 50; })` returns 8.
3. `upperBound(a, 7)`, which is 4.
4. `upperBound(a, 6)`, which is 4.

### Problem E

The probes are index 2 (yes), so `hi = 2`; then index 1 (no), so `lo = 2`; the result is 2. It is not the first "yes" (index 0). What holds is the invariant: the answer at index 1 is "no" and the answer at index 2 is "yes": a boundary.

### Problem F

The test `a[i] <= 7` on `[9, 7, 7, 4, 1]` gives `no, yes, yes, yes, yes`, so `firstTrue` returns 1. The test `a[i] < 7` gives `no, no, no, yes, yes`: 3. The template is unchanged; only the test follows the order of the data.

### Problems 2 to 6

2. `return lowerBound(nums, target);`
3. `l = lowerBound(a, x)`. If `l == n` or `a[l] != x` return `[-1, -1]`, else `[l, upperBound(a, x) - 1]`.
4. `firstTrue<long long>(1, n + 1, isBadVersion)`. In `int`, use `lo + (hi - lo) / 2`.
5. `start = firstTrue(0, n - k, i => x - a[i] <= a[i + k] - x)`, and the answer is `a[start .. start + k)`. The test says the window starting at `i` is already at least as good as the one starting at `i + 1` (a tie keeps the smaller elements); it goes no, no, yes, yes.
6. For a spell `s` the potions that work are those at least `need = ⌈success / s⌉`; the count is `m - lowerBound(potions, need)` (the lower bound is `m` when `need` is above every potion). Compute `need` in 64 bits.

---

## Cheat sheet

- **Template:** `while (lo < hi) { mid = lo + (hi - lo) / 2; if (pred(mid)) hi = mid; else lo = mid + 1; } return lo;` on `[lo, hi)`; the answer is `hi` when every test says "no".
- **Invariant:** once moved, "no" at `lo - 1` and "yes" at `hi`. Progress: `lo <= mid < hi`.
- **Bounds:** `lowerBound`: first `a[i] >= x`. `upperBound`: first `a[i] > x`. Count is the difference; last position is `upperBound - 1`.
- **Cost:** ⌊log2 n⌋ + 1 probes; but each probe is a branch and, in a big array, a cache miss.
- **Traps:** `(lo + hi) / 2`; `lo = mid` with the middle rounded down; `hi = n - 1` when `n` is a valid answer; `size_t` minus 1.

**Sources** (checked while writing this tutorial):
- Joshua Bloch, [Nearly All Binary Searches and Mergesorts are Broken](https://research.google/blog/extra-extra-read-all-about-it-nearly-all-binary-searches-and-mergesorts-are-broken/) (June 2006): the `(low + high) / 2` bug and fixes, the 1946 and 1962 dates and the classroom story, both attributed there to Bentley's *Programming Pearls*, whose own page I could not read (paywalled).
- cppreference, [`std::lower_bound`](https://en.cppreference.com/cpp/algorithm/lower_bound) (partitioned-range precondition, log2(N) + O(1) comparisons, linear iterator steps for non-random-access iterators, the member functions of `std::set` and `std::map`) and [`std::midpoint`](https://cppreference.com/cpp/numeric/midpoint) (no overflow, rounds toward the first argument).
- Go, [`sort.Search`](https://golang.google.cn/src/sort/search.go?m=text) (smallest `i` with `f(i)` true, `n` if none, the `uint(i+j) >> 1` middle). Python, [`bisect`](https://docs.python.org/3/library/bisect.html). [`bsearch(3)`](https://www.man7.org/linux/man-pages/man3/bsearch.3.html) (which equal element is returned is unspecified). SitePoint, [Java's Binary Search API in Five Minutes](https://www.sitepoint.com/javas-binary-search-api-tutorial) (`Arrays.binarySearch`: any match, `-(insertion point) - 1`).
- [`git bisect` documentation](https://code.googlesource.com/git/+/07f14d31abeb00437c2e5d3f6074d56582957e1d/Documentation/git-bisect.txt) (binary search, the "roughly 10 steps" example). Linux [`lib/bsearch.c`](https://android-kvm.googlesource.com/linux/+/refs/tags/v5.8/lib/bsearch.c) (v5.8).
- Algorithmica, [Binary Search](https://en.algorithmica.org/hpc/data-structures/binary-search/) (branch-free search, why it loses its edge on large arrays, Eytzinger layout).

**Next:** tutorial 02, binary search on the answer (monotone predicates: minimum eating speed, ship capacity, split array largest sum). Say "next" when you are ready.
