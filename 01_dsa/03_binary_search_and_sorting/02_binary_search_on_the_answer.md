# DSA 03.02: Binary Search on the Answer

*Part of the [Binary search and sorting plan](00_plan.md) · Time: about 5 hours (2 reading, 3 problems) · You need: [Binary search templates](01_binary_search_templates.md) (`firstTrue` and its invariant) · Companion code: [`02_binary_search_on_the_answer_demo.cpp`](02_binary_search_on_the_answer_demo.cpp)*

By the end of this tutorial you can:

- turn "find the smallest (or largest) number that works" into a yes/no test and one call to `firstTrue`;
- say, before you search, whether the test is monotone and in which direction;
- choose the search range from the problem, including the case where no answer exists;
- write the test for the standard problems: eating speed, shipping capacity, splitting an array, bouquets, placing balls;
- search with a *count* as the test (k-th smallest in a sorted matrix), and search real numbers without an endless loop;
- find an upper bound when the problem gives none, and recognise tests that are not monotone.

The functions are in the companion file. It checks each of them against brute force, and the splitting ones also against an independent dynamic program, on every small input it can enumerate and on the largest inputs the problems allow, and it prints every number quoted below. It compiles with `-Wall -Wextra -fsanitize=address,undefined`.

---

## 1. The idea: guess, then test

In tutorial 01, `firstTrue` found the place where a column of answers flips from "no" to "yes". Nothing in it needs an array. It needs a range of numbers and a yes/no question.

Many problems ask for "the smallest X such that the job can be done", or "the largest X such that ...". Computing X directly is hard. *Testing* a guess is easy: given X, can the job be done? And if X works, then (for a "smallest" problem) so does anything bigger. So as X grows, the answers go no, no, no, yes, yes, yes, and the smallest X that works is the first "yes".

The recipe, which every section below follows:

1. Write the goal as "the smallest X with `ok(X)`" (for a largest X, see section 5).
2. Write `ok(X)`. Given a guess, decide yes or no, usually with one greedy pass over the input.
3. Check that `ok` is **monotone**: if `ok(X)` then `ok(X + 1)`. Say why in one sentence.
4. Pick the range `[lo, hi)`: `lo` is the smallest X that could be right, `hi` is one past the largest that could be needed.
5. Call `firstTrue(lo, hi, ok)`. It makes at most ⌊log2(hi - lo)⌋ + 1 calls to `ok`, so the cost is that times the cost of one `ok`.

Step 3 is the one that decides whether this works. If `ok` is not monotone, the search still returns *a* boundary (tutorial 01, section 6), but not the one you wanted.

Here is the function again, since every call below uses it:

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

## 2. Koko eating bananas (LeetCode 875)

There are `n` piles of bananas and `h` hours. Each hour Koko picks one pile and eats up to `k` bananas from it; if the pile has fewer, she finishes it and waits out the hour. Find the smallest `k` that finishes everything within `h` hours. For piles `[3, 6, 7, 11]` and `h = 8` the answer is 4.

### 2.1 The test

At speed `k` a pile of `p` bananas takes ⌈p / k⌉ hours, so the whole job takes the sum of those. The test is "that sum is at most `h`":

```cpp
// Hours needed to eat every pile at k bananas per hour. 64-bit: 10^4 piles of 10^9 at speed 1 take 10^13 hours.
int64_t hoursNeeded(const std::vector<int>& piles, int64_t k) {
    int64_t hours = 0;
    for (int p : piles) hours += (p + k - 1) / k;               // ceil(p / k)
    return hours;
}
```

For `[3, 6, 7, 11]` the hours at each speed are:

| speed k | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| hours | 27 | 15 | 10 | 8 | 8 | 6 | 5 | 5 | 5 | 5 | 4 | 4 |

With `h = 8` the answers to "hours ≤ 8?" read no, no, no, **yes**, yes, yes, ... The first "yes" is speed 4.

**Why it is monotone.** A larger bite never needs more hours for a pile: ⌈p / k⌉ does not grow with `k`. So the sum does not grow either, and once a speed is fast enough, every faster speed is too.

### 2.2 The range, and the search

The slowest sensible speed is 1 (speed 0 eats nothing). The fastest we ever need is the biggest pile: at that speed every pile takes one hour, so the job takes `n` hours, and the problem promises `h ≥ n`. So the answer is in `[1, max]`, and the range for `firstTrue` is `[1, max + 1)`:

```cpp
// The smallest speed that finishes within h hours. Speed max(piles) always works if h >= number of piles
// (one hour per pile), so the answer is in [1, max]. If h is too small, the result is max + 1: "none".
int64_t minEatingSpeed(const std::vector<int>& piles, int64_t h) {
    int64_t top = *std::max_element(piles.begin(), piles.end());
    return firstTrue<int64_t>(1, top + 1, [&](int64_t k) { return hoursNeeded(piles, k) <= h; });
}
```

If `h` is smaller than the number of piles, nothing works, and the call returns `max + 1`: the "none" value of tutorial 01, for free.

The search on `[3, 6, 7, 11]` with `h = 8`, over the range `[1, 12)`:

| step | lo | hi | k = mid | hours | hours <= 8 | then |
|---|---|---|---|---|---|---|
| 1 | 1 | 12 | 6 | 6 | yes | `hi = 6` |
| 2 | 1 | 6 | 3 | 10 | no | `lo = 4` |
| 3 | 4 | 6 | 5 | 8 | yes | `hi = 5` |
| 4 | 4 | 5 | 4 | 8 | yes | `hi = 4` |
| end | 4 | 4 | | | | return 4 |

Four tests for eleven candidates (the speeds 1 to 11). The demo prints the same probes: `6:T 3:F 5:T 4:T -> 4`.

### 2.3 What it costs, and the 64-bit trap

Each test is one pass over the piles, `n` steps. The range holds `max` candidates, so the search makes at most ⌊log2 max⌋ + 1 tests: at most 30 for `max = 10^9`. With `n = 10^4` piles that is about 3 × 10^5 steps in all. Trying every speed from 1 upwards would take up to 10^9 × 10^4 = 10^13.

The demo runs the largest input the problem allows: 10^4 piles of 10^9 bananas with `h = 10^9`. The answer is 10 000, found in 30 tests. The same input shows why `hoursNeeded` returns 64 bits: at speed 1 the total is 10^13 hours, which would wrap to 1 316 134 912 in 32 bits, and the comparison with `h` would mean nothing.

---

## 3. Shipping packages within D days (LeetCode 1011)

Packages with given weights sit on a belt and must be shipped *in that order*. Each day the ship carries a run of consecutive packages whose total weight is at most its capacity. Find the smallest capacity that ships everything within `days` days. For weights `1, 2, ..., 10` and 5 days the answer is 15.

### 3.1 The test

Given a capacity `c`, how many days does the job take? Load greedily: add packages to today's load until the next one does not fit, then start a new day.

```cpp
// Days needed to ship the packages in the given order with a ship of capacity c: load each day as much as fits.
// Only meaningful when c is at least the heaviest package.
int daysNeeded(const std::vector<int>& w, int64_t c) {
    int days = 1;
    int64_t load = 0;
    for (int x : w) {
        if (load + x > c) { ++days; load = 0; }                 // x does not fit today: start the next day
        load += x;
    }
    return days;
}
```

**Why greedy is right.** Compare it with any other plan that uses the same capacity. After day 1 the greedy ship has carried at least as many packages as the other plan, because it stops only when the next package truly does not fit. The same holds after day 2, and so on (each day it starts at least as far along and takes as many as can fit). So it never needs more days. The demo agrees: for every list of up to five weights it matches an independent dynamic program that tries all ways to cut the list.

**Why it is monotone.** A bigger ship can use the loading of a smaller one (every day's load still fits), so the number of days never grows with the capacity.

### 3.2 The range, and the search

The smallest capacity that can work is the heaviest package: anything less cannot carry it. The largest we ever need is the total weight (one day). So:

```cpp
// The smallest capacity that ships everything within `days` days. It cannot be below the heaviest package
// and never needs to be above the total weight (one day).
int64_t shipCapacity(const std::vector<int>& w, int days) {
    int64_t lo = *std::max_element(w.begin(), w.end());
    int64_t hi = std::accumulate(w.begin(), w.end(), int64_t(0));
    return firstTrue<int64_t>(lo, hi + 1, [&](int64_t c) { return daysNeeded(w, c) <= days; });
}
```

Why not start at 1? Because `daysNeeded` is only meaningful when every package fits. For capacity 1 it still "ships" a package of weight 5, as a day of its own. On weights `[1, 5]` with 2 days, a search starting at 1 returns **1**: a ship that cannot carry the 5. The demo shows this; it is Problem B1.

The search on weights `1..10` with 5 days over `[10, 56)` probes `33:T 21:T 15:T 12:F 14:F -> 15`. At capacity 15 the days are `1 2 3 4 5 | 6 7 | 8 | 9 | 10`, which is five. At 14 the loads are `1 2 3 4 | 5 6 | 7 | 8 | 9 | 10`, which is six days.

### 3.3 Cost

The problem allows up to 5 × 10^4 packages of weight at most 500, so the range is at most 2.5 × 10^7 wide. The search makes at most 25 tests (the demo's worst input, all weights 500 and one day, needed 24), each a pass of 5 × 10^4 steps: about 1.25 × 10^6 steps. Trying each capacity in turn: up to 2.5 × 10^7 × 5 × 10^4 = 1.25 × 10^12.

---

## 4. Split array largest sum (LeetCode 410): the same problem

Split an array into `k` non-empty runs of consecutive elements so that the largest run sum is as small as possible. For `[7, 2, 5, 10, 8]` and `k = 2` the answer is 18, from `[7, 2, 5]` and `[10, 8]`.

Read it as the ship problem: the run sum is the ship's load, the capacity is the largest allowed run sum, and the days are the runs. A capacity works when the greedy loading needs at most `k` runs. So the code is one line:

```cpp
// Split a into k non-empty contiguous parts so that the largest part sum is as small as possible. This is the ship
// problem: the capacity is the largest part sum and the days are the parts. (At most k parts is the same as exactly k
// when k <= n, because cutting a part in two never raises the largest sum.)
int64_t splitLargestSum(const std::vector<int>& a, int k) { return shipCapacity(a, k); }
```

(The problem says exactly `k` runs and `daysNeeded` counts *at most* `k`. These agree when `k` is at most the length of the array: if a split into fewer runs has largest sum `S`, cutting one of its runs in two cannot raise the largest sum above `S`. Elements may be 0, and the code handles that.)

The search for `[7, 2, 5, 10, 8]` and `k = 2` runs over `[10, 33)` and probes `21:T 15:F 18:T 17:F -> 18`. The answers for `k = 1, 2, 3, 4, 5` are 32, 18, 14, 10, 10: more runs never make the largest sum bigger, and with `k = 5` it is the largest single element.

The textbook solution is a dynamic program over (number of runs, prefix length), about k × n² / 2 steps: 2.5 × 10^7 for `n = 1000` and `k = 50`. The demo uses it as the reference answer. The search needs at most 30 tests (the range is at most the total, 10^9) of `n` steps each: about 3 × 10^4.

The shape to recognise: "**minimise the largest** of some parts" or "**maximise the smallest**". Both are searches on that largest (or smallest) value, with a greedy pass as the test.

---

## 5. Maximising: magnetic force between two balls (LeetCode 1552)

Baskets stand at given positions. Put `m` balls into distinct baskets so that the *smallest* distance between two balls is as **large** as possible. For positions `[1, 2, 3, 4, 7]` and `m = 3` the answer is 3 (balls at 1, 4 and 7).

### 5.1 The test points the other way

Let `canPlace(d)` mean "m balls fit with every two at least `d` apart". Take the first basket, then the next basket that is at least `d` beyond the last ball, and so on:

```cpp
// Can m balls go into the sorted positions with every two at least d apart? Take the first position, then every
// position that is at least d beyond the last one taken.
bool canPlace(const std::vector<int>& pos, int m, int d) {
    int placed = 1, last = pos[0];
    for (size_t i = 1; i < pos.size(); ++i)
        if (pos[i] - last >= d) { ++placed; last = pos[i]; }
    return placed >= m;
}
```

A large `d` is harder, so `canPlace` is true for small `d` and false for large `d`: yes, yes, yes, no, no. That is monotone, but the wrong way round for `firstTrue`, which wants no, no, yes, yes. We want the *last* yes.

### 5.2 Turn the question around

Ask the opposite question: "is `d` **too large**?", which is `!canPlace(d)`. Its answers run no, no, no, yes, yes, so `firstTrue` finds the first `d` that is too large, and the answer is the one before it:

```cpp
// The largest minimum distance. canPlace is true for small d and false for large d, so search for the first d
// that is too large and step back one. m balls need m - 1 gaps, so d is at most span / (m - 1).
int maxMinDistance(std::vector<int> pos, int m) {
    std::sort(pos.begin(), pos.end());
    int hi = (pos.back() - pos.front()) / (m - 1);
    return firstTrue<int>(1, hi + 1, [&](int d) { return !canPlace(pos, m, d); }) - 1;
}
```

The range: `d = 1` always works (the positions are distinct whole numbers, so any two baskets are at least 1 apart), and `m` balls need `m - 1` gaps of at least `d`, so `d` is at most the span divided by `m - 1`. If every `d` up to that limit works, `firstTrue` returns `limit + 1`, and the `- 1` turns it back into `limit`.

For `[1, 2, 3, 4, 7]` and `m = 3` the span is 6, the limit is 3, and the search over `[1, 4)` probes `2:F 3:F -> 4`: both 2 and 3 are fine, so the first "too large" is 4 and the answer is 3.

**Why not a "last true" loop?** Tutorial 01 (section 5.2) showed one; it must round the middle *up*, or `lo = mid` stalls: here, with `lo = 2` and `hi = 3`, `canPlace(2)` is true, the middle is 2, and the loop never ends (Problem B2). Negating the test keeps the one template you have already proved. Whenever a problem asks for the largest X, search for the first X that is too large and subtract one.

The greedy placement is right because moving a ball to the earliest allowed basket never leaves less room for the next ones. The demo checks it against trying every choice of `m` baskets, for every subset of positions 1 to 10.

---

## 6. When the answer may not exist: bouquets (LeetCode 1482)

Flower `i` blooms on day `bloomDay[i]`. A bouquet needs `k` *adjacent* flowers, all bloomed, and each flower goes into one bouquet. Find the earliest day on which `m` bouquets can be made, or -1 if that never happens. For `[1, 10, 3, 10, 2]`, `m = 3` and `k = 1` the answer is 3; with `k = 2` it is -1.

**The test** scans the row. A flower is open on day `d` if `bloomDay ≤ d`. Count the length of the current run of open flowers; each time it reaches `k`, one bouquet is made and the run starts again:

```cpp
// Are there m bouquets of k adjacent flowers by day d? A flower is open when bloomDay <= d.
bool canMakeBouquets(const std::vector<int>& bloomDay, int64_t m, int k, int d) {
    int64_t made = 0;
    int run = 0;                                                // adjacent open flowers in the current run
    for (int b : bloomDay) {
        if (b > d) run = 0;
        else if (++run == k) { ++made; run = 0; }
    }
    return made >= m;
}
```

It is monotone: a flower open on day `d` is still open on day `d + 1`, so any bouquet you could make stays possible.

**The range.** Before the first bloom nothing is open; the best case is the day the last flower blooms, when everything is. So search `[first bloom, last bloom + 1)`. If even the last bloom day does not give `m` bouquets, no day does, and `firstTrue` returns the end of the range, `last bloom + 1`. That is the "none" value, so it becomes -1:

```cpp
// The first day with m bouquets, or -1. Searching [first bloom, last bloom + 1) returns last bloom + 1 when even
// the last day is not enough, and that is exactly "none".
int minDays(const std::vector<int>& bloomDay, int64_t m, int k) {
    int lo = *std::min_element(bloomDay.begin(), bloomDay.end());
    int hi = *std::max_element(bloomDay.begin(), bloomDay.end()) + 1;
    int day = firstTrue<int>(lo, hi, [&](int d) { return canMakeBouquets(bloomDay, m, k, d); });
    return day == hi ? -1 : day;
}
```

The demo prints the two searches. With `k = 1` the probes `6:T 3:T 2:F` end at 3. With `k = 2` they are `6:F 9:F 10:F` and the result is 11, the end of the range, so the answer is -1.

Many solutions first test `m * k > n` and return -1. The demo confirms that the answer is -1 exactly when `m * k > n`, but that product can reach 10^6 × 10^5 = 10^11, which overflows a 32-bit `int`. The template needs no product at all.

---

## 7. A count as the test: k-th smallest in a sorted matrix (LeetCode 378)

An `n × n` matrix has every row and every column sorted ascending. Find the k-th smallest entry, counting repeats. For `[[1, 5, 9], [10, 11, 13], [12, 13, 15]]` and `k = 8` the answer is 13.

Up to now we searched *speeds*, *capacities* and *distances*. Here we search the **values** themselves. The test is "at least `k` entries are ≤ `x`". As `x` grows the count can only grow, so the answers go no, no, yes, yes, and the first `x` that says yes is the answer. That `x` is always an entry of the matrix: if it were not, the count at `x - 1` would be the same as at `x`, and `x - 1` would already say yes.

**Counting in O(n).** Because rows and columns are sorted, start at the bottom-left corner. If the entry is ≤ `x`, everything above it in its column is too: add them all and step right. Otherwise step up. Each step moves one way, so there are at most 2n of them:

```cpp
// How many entries are <= x? Rows and columns are sorted. Start at the bottom-left corner: if the entry is <= x, the
// whole column above it is too, so add them and step right; otherwise step up. At most 2n steps.
int64_t countAtMost(const std::vector<std::vector<int>>& mat, int x) {
    int n = (int)mat.size();
    int64_t count = 0;
    int r = n - 1, c = 0;
    while (r >= 0 && c < n) {
        if (mat[r][c] <= x) { count += r + 1; ++c; }
        else --r;
    }
    return count;
}
```

For `x = 12` on the matrix above: 12 is ≤ 12, so add 3 (column 0) and step right; 13 is too big, step up; 11 fits, add 2 and step right; 13 is too big, step up; 9 fits, add 1. Total 6. For 13 the count is 8, and for 14 it is also 8.

```cpp
// The k-th smallest entry, k from 1. The smallest x with countAtMost(x) >= k is always an entry of the matrix:
// if x were not an entry, countAtMost(x - 1) would equal countAtMost(x), and x - 1 would already qualify.
// The values may be +-10^9, so hi - lo stays below 2^31 - 1 (it is at most 2 * 10^9 + 1).
int kthSmallest(const std::vector<std::vector<int>>& mat, int64_t k) {
    int lo = mat[0][0], hi = mat.back().back() + 1;
    return firstTrue<int>(lo, hi, [&](int x) { return countAtMost(mat, x) >= k; });
}
```

The search for `k = 8` runs over `[1, 16)` and probes `8:F 12:F 14:T 13:T -> 13`: the counts are 2, 6, 8 and 8. The range is the smallest entry up to the largest entry plus one. With values up to ±10^9, `hi - lo` is at most 2 × 10^9 + 1, which is below 2^31 - 1 = 2 147 483 647, so `int` is enough; the demo prints both numbers. The cost is about 2n steps per test and about 31 tests.

When a problem asks for "the k-th", "the median" or "the value such that half ...", ask whether you can *count* how many things are at most `x`. If you can, and the count is cheap, the count is your test.

---

## 8. Real numbers

If the answer is a real number there is no "mid + 1" and no last candidate. The interval just keeps halving. A loop with a fixed number of rounds is the safe form:

```cpp
// Square root by halving a fixed number of times. Real numbers have no "mid + 1", so the loop runs a set number
// of rounds: the interval [lo, hi] shrinks to (hi - lo) / 2^rounds. The predicate is "mid * mid >= x".
double sqrtReal(double x, int rounds) {
    double lo = 0, hi = std::max(1.0, x);
    for (int i = 0; i < rounds; ++i) {
        double mid = lo + (hi - lo) / 2;
        if (mid * mid >= x) hi = mid;
        else lo = mid;
    }
    return hi;
}
```

After `r` rounds the interval `[lo, hi]` is `(hi - lo) / 2^r` wide, so the rounds needed for a width `eps` are ⌈log2((hi - lo) / eps)⌉. For `x = 2` the interval starts 2 wide; to reach 10^-9 takes ⌈log2(2 × 10^9)⌉ = 31 rounds. The value of `hi` after rounds 1 to 7 is 2, 1.5, 1.5, 1.5, 1.4375, 1.4375, 1.421875, and after 31 rounds `sqrtReal(2, 31)` is 1.414213562384, which is 1.1 × 10^-11 from the true root.

**The tempting loop, and why it can run forever.** Many people write `while (hi - lo > eps)`:

```cpp
// The tempting version: stop when hi - lo <= eps. The cap exists only so that the demo can stop it; without it
// this loop can run forever.
int roundsUntil(double x, double eps, int cap, double& width) {
    double lo = 0, hi = std::max(1.0, x);
    int rounds = 0;
    while (hi - lo > eps && rounds < cap) {
        double mid = lo + (hi - lo) / 2;
        if (mid * mid >= x) hi = mid;
        else lo = mid;
        ++rounds;
    }
    width = hi - lo;
    return rounds;
}
```

For `x = 2` and `eps = 10^-9` it stops after the same 31 rounds. But take `x = 10^18`, whose root is 10^9. Doubles near 10^9 are spaced 2^-23 = 1.19 × 10^-7 apart. Once `hi` and `lo` are neighbours, the middle rounds to one of them and the interval stops shrinking. With `eps = 10^-9` the loop would never end: the demo cuts it off after 1000 rounds with the width still at 1.19 × 10^-7. With `eps = 10^-6` it ends after 80 rounds, at a width of 8.3 × 10^-7.

So: either fix the number of rounds (100 is a common choice: it cuts the interval by 2^100, which is far below what a double can hold, and more rounds change nothing; the demo checks that 100 and 200 rounds give the same answer), or make `eps` relative to the size of the numbers.

---

## 9. Setting it up

### 9.1 The problems side by side

| Problem | What you search | `ok(X)` | Kind | `lo` | `hi` (exclusive) |
|---|---|---|---|---|---|
| Koko | eating speed | hours ≤ h | smallest | 1 | largest pile + 1 |
| Ship | capacity | days ≤ D | smallest | heaviest package | total weight + 1 |
| Split array | largest run sum | runs ≤ k | smallest | largest element | total + 1 |
| Balls | smallest distance | `canPlace` | **largest**: search `!ok`, subtract 1 | 1 | span / (m - 1) + 1 |
| Bouquets | day | `canMakeBouquets` | smallest | first bloom | last bloom + 1 (the end means none) |
| k-th in matrix | a value | count ≤ x is at least k | smallest | smallest entry | largest entry + 1 |
| Square root | a real number | mid × mid ≥ x | smallest | 0 | max(1, x) |

### 9.2 Choosing the ends

- **Both ends must bracket the answer.** A `hi` that is too small clips the answer and gives a wrong result with no warning. A `lo` that is too small costs only about one extra test, *as long as the test can handle it*: Koko with `lo = 0` divides by zero, and the ship with `lo = 1` gives a wrong answer (section 3.2).
- **The end is exclusive, and `firstTrue` returns it when nothing is true.** Writing `top` instead of `top + 1` also works when the answer can equal `top`: "none" is then the right answer. But "nothing works" and "the answer is `top`" then look the same. With `top + 1`, Koko on `[3, 6, 7, 11]` and `h = 3` returns 12, which is plainly out of range (the demo checks it); with `top` it would return 11, which looks like an answer.
- **Use 64 bits** for the totals the test adds up (hours, weights) and check that `hi - lo` stays below 2^31 - 1 if the range is `int`.

### 9.3 No upper bound given

Sometimes nothing tells you how large the answer can be. Double a guess until the test says yes, then search the last step. The answer is then in `(hi / 2, hi]`:

```cpp
// The first x >= 1 with pred(x), when pred is false, ..., false, true, ... and nothing says how far the "true" part
// starts. Double until pred holds, then search the last doubling step. (Loops forever if pred is never true.)
template <class Pred>
int64_t firstTrueFrom1(Pred pred) {
    int64_t hi = 1;
    while (!pred(hi)) hi *= 2;                                  // the answer is in (hi / 2, hi]
    return firstTrue<int64_t>(hi / 2 + 1, hi + 1, pred);
}
```

This costs at most about twice as many tests as a search with a known bound: for an answer of 1000 the demo makes 20 (11 doublings, 1 to 1024, then 9 in the search). It loops forever if the test is never true, so use it only when you know the answer exists. Tutorial 03 calls this *exponential search*.

### 9.4 When the test is not monotone

The most common mistake is asking for equality. "Finish in *exactly* `h` hours" sounds like the same question as "within `h` hours", but the hours at each speed (section 2.1) are 27, 15, 10, **8, 8**, 6, 5, ... so the answer to "hours == 8?" is no for speeds 1 to 3, yes for 4 and 5, then no again. The first probe, speed 6, takes 6 hours: "no", so the search moves right, away from the answer, and returns 12, the end of the range. The demo prints it. **Ask for "at most" or "at least", never "exactly".**

A subtler case: "the smallest number of threads that reaches a target throughput". If more threads can make things *slower* (lock contention, cache thrashing), the answers are not monotone, and no binary search can be trusted. Check the monotonicity of the real system, not only of the model.

---

## 10. Where this shows up

- **Finding the largest batch size that fits in GPU memory.** PyTorch Lightning's `BatchSizeFinder` has a `binsearch` mode. Its documentation says it doubles the batch size until a run hits an out-of-memory error, then does a binary search between the last size that worked and the one that failed, and by default takes 5% off the result as a safety margin. That is section 9.3 followed by section 5: the test is "does one step fit in memory?", and it is monotone as long as memory grows with the batch size.
- **Sizing from a recorded trace** (fits to the problem, not claims about a product): the smallest buffer for which a captured packet trace causes no drops; the smallest link rate that delivers a captured frame trace before its deadlines; the earliest time by which machines of different speeds have made a quota (CSES *Factory Machines* is this problem). Each is "replay the trace, count the failures", and for a replayed trace the answer is usually monotone. Check that before you search (section 9.4).
- **Interviews.** This is the pattern behind most "minimise the maximum" and "maximise the minimum" questions, and it is why the problems below look so alike.

---

## 11. Mistakes that cost points

1. **Not saying which way the test is monotone.** Write "if `ok(X)` then `ok(X + 1)`" (or the reverse) and one sentence of why, before any code.
2. **A test with `==`.** Use "at most" or "at least" (section 9.4).
3. **A `lo` the test cannot handle.** Ship capacity below the heaviest package, speed 0, distance 0.
4. **A `hi` that is too small, or no way to say "none".** Search to the largest possible answer plus one, and decide what the end of the range means.
5. **32-bit totals.** Hours in Koko, weights in a shipment, `m * k` in the bouquets.
6. **Maximising with `lo = mid` and a middle that rounds down** (it stalls), or forgetting the `- 1` after negating the test. Search for the first too-large value and subtract 1.
7. **A greedy test you did not argue.** A binary search over a wrong test gives a confident wrong answer.
8. **`while (hi - lo > eps)` on real numbers.** Use a fixed number of rounds.

---

## 12. Questions you will be asked

**How do you recognise a binary search on the answer?** The question asks for the smallest X that works, or the largest, or "minimise the maximum" or "maximise the minimum". The range of answers is huge (10^9) but checking one guess is a cheap greedy pass, and a bigger guess is never worse (or never better).

**Why is the ship test monotone, and why is the greedy loading correct?** A bigger ship can reuse the loading of a smaller one. The greedy loading, which fills each day as much as it can, has carried at least as many packages as any other plan after every day, so it uses the fewest days.

**How do you search a real-valued answer?** A fixed number of halvings, not an epsilon test, and say how many rounds the precision needs: ⌈log2(width / eps)⌉.

**You do not know an upper bound. What do you do?** Double a guess until the test passes, then search inside the last doubling (section 9.3).

**The test is not monotone. Now what?** Then a binary search is not justified. Look for a different quantity that is monotone, or a different method.

---

## 13. Problems

**How to use this section.** Try each problem for 15 minutes (easy) or 30 minutes (harder) before reading anything. If you are stuck, read only the **How** lines, then try again, and only then open the answer at the bottom. Log each attempt in the [practice log](../practice_log.md).

### On paper

**Problem A: Trace.** Koko with piles `[30, 11, 23, 4, 20]`. Run the search over `[1, 31)` for `h = 5` and for `h = 6`: write the probes and the answers.
*Where:* paper. *How:* the table of 2.2; for each probe add up the ⌈p / k⌉.

**Problem B: Find the bug.** Each function should do what its comment says. For each, give an input that breaks it and say what happens. (`daysNeeded`, `canPlace` and `hoursNeeded` are the functions of sections 2, 3 and 5.)

```cpp
// 1. the smallest capacity that ships everything within `days` days
int64_t f1(const std::vector<int>& w, int days) {
    int64_t hi = std::accumulate(w.begin(), w.end(), int64_t(0));
    return firstTrue<int64_t>(1, hi + 1, [&](int64_t c) { return daysNeeded(w, c) <= days; });
}

// 2. the largest d for which canPlace holds (pos is sorted, m >= 2)
int f2(const std::vector<int>& pos, int m) {
    int lo = 1, hi = (pos.back() - pos.front()) / (m - 1);
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (canPlace(pos, m, mid)) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

// 3. the smallest speed that finishes within h hours
int64_t f3(const std::vector<int>& piles, int64_t h) {
    int64_t top = *std::max_element(piles.begin(), piles.end());
    return firstTrue<int64_t>(1, top + 1, [&](int64_t k) { return hoursNeeded(piles, k) == h; });
}
```
*Where:* paper. *How:* try each on a tiny input; compare with sections 3.2, 5.2 and 9.4.

**Problem C: Monotone or not?** For each question about a guess `X`, say whether the answers are monotone as `X` grows, and which way, or give a counterexample: (1) "Koko finishes in exactly `h` hours at speed `X`"; (2) "some run of `X` consecutive elements of an array of positive numbers has sum at least `S`"; (3) "`X` is a perfect square"; (4) "at least `k` entries of a sorted matrix are at most `X`"; (5) "`m` balls fit with every two at least `X` apart".
*Where:* paper. *How:* try small cases; ask whether a "yes" at `X` forces a "yes" at `X + 1`, or a "yes" at `X - 1`.

**Problem D: Bounds.** Give `lo` and `hi` (exclusive) for: (1) Koko with piles up to 10^9; (2) Find the Smallest Divisor Given a Threshold (LeetCode 1283: the smallest `d` such that the sum of ⌈a / d⌉ over the array is at most `threshold`; `threshold` is at least the length of the array and `a` is at most 10^6); (3) Minimum Number of Days to Make m Bouquets; (4) the k-th smallest of a matrix whose entries are between -10^9 and 10^9.
*Where:* paper. *How:* the table of 9.1 and the rules of 9.2.

**Problem E: Cost.** A shipment has 5 × 10^4 packages, each at most 500. How many tests can the capacity search make at most, how many steps is that in all, and how many would trying every capacity take?
*Where:* paper. *How:* section 3.3; the range is at most the total weight.

**Problem F: Rounds.** How many halvings does `sqrtReal` need to bring the interval for `x = 2` below 10^-12?
*Where:* paper (then check with `roundsUntil`). *How:* section 8; the interval starts 2 wide.

### In code

**Problem 1: Koko Eating Bananas** (LeetCode 875, medium)
*Where:* leetcode.com/problems/koko-eating-bananas. *Goal:* the smallest speed that finishes within `h` hours. Then write a brute force that tries every speed and compare the two on all pile lists of up to four piles over 1 to 6.
*How:* sections 1 and 2. The total hours need 64 bits.

**Problem 2: Capacity To Ship Packages Within D Days** (LeetCode 1011, medium)
*Where:* leetcode.com/problems/capacity-to-ship-packages-within-d-days. *Goal:* the smallest capacity that ships within `days` days.
*How:* section 3. Start `lo` at the heaviest package.

**Problem 3: Split Array Largest Sum** (LeetCode 410, hard)
*Where:* leetcode.com/problems/split-array-largest-sum. *Goal:* the smallest possible largest run sum. Then write the dynamic program (best largest sum for the first `i` elements in `j` runs) and compare it with your search on random arrays.
*How:* section 4: it is Problem 2 with `k` as the number of days.

**Problem 4: Minimum Number of Days to Make m Bouquets** (LeetCode 1482, medium)
*Where:* leetcode.com/problems/minimum-number-of-days-to-make-m-bouquets. *Goal:* the earliest day with `m` bouquets, or -1.
*How:* section 6. Let the end of the range mean "none".

**Problem 5: Find the Smallest Divisor Given a Threshold** (LeetCode 1283, medium)
*Where:* leetcode.com/problems/find-the-smallest-divisor-given-a-threshold. *Goal:* the smallest divisor whose rounded-up quotients sum to at most `threshold`.
*How:* the test is a sum of ⌈a / d⌉, like Koko's hours. A larger divisor makes every quotient no bigger.

**Problem 6: Magnetic Force Between Two Balls** (LeetCode 1552, medium)
*Where:* leetcode.com/problems/magnetic-force-between-two-balls. *Goal:* the largest possible smallest distance.
*How:* section 5. Sort the positions first; search for the first distance that is too large.

**Problem 7: Kth Smallest Element in a Sorted Matrix** (LeetCode 378, medium)
*Where:* leetcode.com/problems/kth-smallest-element-in-a-sorted-matrix. *Goal:* the k-th smallest entry, in O(n log(range)) time and O(1) extra memory.
*How:* section 7. The test is the count of entries at most `x`, computed from the corner.

**More practice:** Minimum Speed to Arrive on Time (1870, real-valued hours), Minimum Limit of Balls in a Bag (1760), Minimized Maximum of Products Distributed to Any Store (2064), Minimum Time to Complete Trips (2187), Find K-th Smallest Pair Distance (719), Kth Smallest Number in Multiplication Table (668); CSES *Factory Machines* and *Array Division*.

---

## Answers

### Problem A

For `h = 5` the probes are `16:F 24:F 28:F 30:T 29:F`, so the answer is **30**: at 29 the pile of 30 takes 2 hours and the total is 6. For `h = 6` the probes are `16:F 24:T 20:F 22:F 23:T`, so the answer is **23**.

### Problem B

1. `f1` starts at 1, and `daysNeeded` counts a package heavier than the ship as a day of its own. For `[1, 5]` and 2 days it returns **1**: a ship of capacity 1 cannot carry the 5. Start at the heaviest package.
2. `f2` is the "last true" loop with a middle that rounds down. For `[1, 2, 3, 4, 7]` and `m = 3`: `lo = 2`, `hi = 3`, `canPlace(2)` is true, the middle is 2, `lo` stays 2, and the loop **never ends**.
3. `f3` tests `==`. For `[3, 6, 7, 11]` and `h = 8` it returns **12**, the end of the range, instead of 4.

### Problem C

1. No: true only for speeds 4 and 5 in the example of 2.1. 2. Yes, from some length on it is true for every longer length up to `n`, because adding a positive element cannot lower the sum. 3. No: 4 is a square, 5 is not, 9 is. 4. Yes, no then yes: the count only grows. 5. Monotone the other way: yes for small `X`, no for large `X`, so search for the first "no" and subtract 1.

### Problem D

1. `[1, 10^9 + 1)`. 2. `[1, max(a) + 1)`: at divisor `max(a)` every quotient is 1, so the sum is the length of the array, which is at most `threshold`. 3. `[first bloom, last bloom + 1)`, where the end means -1. 4. `[smallest entry, largest entry + 1)`, and the width is at most 2 × 10^9 + 1.

### Problem E

The range is at most 2.5 × 10^7 wide, so at most ⌊log2(2.5 × 10^7)⌋ + 1 = 25 tests, each a pass of 5 × 10^4 steps: **1.25 × 10^6** steps. Trying every capacity: up to 2.5 × 10^7 × 5 × 10^4 = **1.25 × 10^12**.

### Problem F

The interval is 2 wide, and ⌈log2(2 / 10^-12)⌉ = ⌈40.86⌉ = **41** rounds. `roundsUntil(2.0, 1e-12, ...)` agrees.

### Problems 1 to 7

1. `firstTrue<long long>(1, max + 1, k => hours(k) <= h)`, with `hours` summing `(p + k - 1) / k`.
2. `firstTrue<long long>(max, sum + 1, c => days(c) <= D)`.
3. The same call with `D = k`. DP: `dp[j][i] = min over t of max(dp[j-1][t], prefix[i] - prefix[t])`.
4. `firstTrue(first, last + 1, canMake)`, and return -1 when the result is `last + 1`.
5. `firstTrue<int>(1, max + 1, d => sum of ceil(a / d) <= threshold)`; sum in 64 bits.
6. `firstTrue<int>(1, span / (m - 1) + 1, d => !canPlace(d)) - 1`, after sorting.
7. `firstTrue<int>(m[0][0], m[n-1][n-1] + 1, x => countAtMost(x) >= k)`, with the corner walk for the count.

---

## Cheat sheet

- **Recipe:** goal "smallest X with `ok(X)`"; show `ok` is monotone; range `[lo, hi)`; `firstTrue(lo, hi, ok)`; cost = (⌊log2(hi - lo)⌋ + 1) × cost of `ok`.
- **Largest X:** search for the first X where `!ok(X)` and subtract 1.
- **No answer:** the call returns `hi`; make `hi` mean "none".
- **Tests:** a greedy pass (speed, capacity, distance, bouquets) or a count (k-th smallest). Never `==`.
- **Real numbers:** a fixed number of halvings, ⌈log2(width / eps)⌉ of them.
- **No upper bound:** double until the test passes, then search the last step.
- **Traps:** a `lo` the test cannot handle, 32-bit totals, a `hi` that clips the answer, `lo = mid` with a middle that rounds down.

**Sources** (checked while writing this tutorial):
- LeetCode problem statements for 875, 1011, 410, 1482, 1283, 1552 and 378 (constraints and examples, read from the [doocs.org mirror](https://leetcode.doocs.org/en/lc/875/) of the statements); titles of 2187, 2064, 1760, 1870, 719 and 668 from [walkccc.me's binary search list](https://walkccc.me/LeetCode/topics/algorithms/search/).
- USACO Guide, [Binary Search on the Answer](https://usaco-guide-git-patch-1.cpinitiative.vercel.app/silver/binary-search-ans) (the answer function must be monotone; the "first true" and "last true" forms; `long long` for large bounds). cp-algorithms, [Binary search](https://gh.cp-algorithms.com/1384/num_methods/binary_search.html) (real-valued search in O(log((R - L) / δ))). CSES, *Factory Machines* and *Array Division*, with the binary-search solution described on the [USACO Guide solution pages](https://usaco-guide-git-simulationredo.cpinitiative.vercel.app/solutions/cses-1620).
- PyTorch Lightning, [`BatchSizeFinder`](https://lightning.ai/docs/pytorch/latest/api/lightning.pytorch.callbacks.BatchSizeFinder.html) (the `power` and `binsearch` modes and the `margin`).

---

## Sources

- Counts, outputs and timings in this tutorial come from the companion `02_binary_search_on_the_answer_demo.cpp` (it prints the number of checks it passed) on the machine described in the text. Times are that machine's, not yours.
- The algorithms and data structures are standard; for further reading see the "Extra practice and resources" section of the [area plan](00_plan.md), which lists the textbook chapters and free references for this area. The explanations and code are my own.
- LeetCode numbers and titles are from memory; check a number before you rely on it.

**Next:** tutorial 03, rotated, peak and 2D search (rotated arrays, peak finding, sorted matrices, median of two sorted arrays, exponential search). Say "next" when you are ready.
