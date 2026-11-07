# DSA 05.03: The Monotonic Deque

*Part of the [Stacks and queues plan](00_plan.md) · Time: about 6 hours (3 reading, 3 problems) · You need: [The monotonic stack](02_monotonic_stack.md) (the waiting room and the 2n argument) and [Stack and queue fundamentals](01_stack_and_queue_fundamentals.md) (05.01 section 3.4, the ring, and 05.01 section 6.3, the min-queue) · Related: [Sliding window](../01_arrays_and_strings/03_sliding_window.md) and [Prefix sums](../01_arrays_and_strings/04_prefix_sums_and_difference_arrays.md) · No companion file: every listing is in this tutorial.*

By the end of this tutorial you can:

- explain why a window needs a deque and not a stack, and why every element enters and leaves the deque once;
- write the sliding window maximum and minimum from one template, on a `std::deque` and on a fixed ring, and say what the alternatives cost;
- build a queue with an O(1) amortised minimum from a deque, and compare it with the two-stack queue of 05.01;
- solve the shortest subarray with sum at least K (negative numbers allowed), the longest subarray with a limit on max minus min, and a DP whose transition is a maximum over a window.

**Where the numbers come from.** Comparison counts are made through a small wrapper type that counts every `<` and `>`; a count is the same on every machine. Timings were measured on one machine: a 2-core Intel Xeon VM at 2.10 GHz (Ubuntu 24.04, GCC 13.3 with `-O2`). Each time is the median of three runs of the program, and each run takes the best of five repetitions (the direct scan, the slowest, best of three). Your numbers will differ; the shapes should not. Every listing was compiled with `-Wall -Wextra` and checked against a brute force: 1,817,725 checks passed under AddressSanitizer with UBSan (GCC 13.3), and the same program passed at `-O2` with GCC and with clang 18. Problem labels (Easy, Medium, Hard) come from mirror sites of the problem statements, because LeetCode itself could not be opened from here.

All listings need these headers: `<algorithm>`, `<deque>`, `<functional>`, `<utility>` and `<vector>`.

---

## 1. The idea

### 1.1 The question

Given an array and a window size `k`, report the maximum of every window of `k` consecutive elements. For `[1, 3, -1, -3, 5, 3, 6, 7]` and `k` = 3 there are six windows, with maxima `[3, 3, 5, 5, 6, 7]`.

The monotonic stack of tutorial 02 answers "nearest bigger" over the whole past. A window adds a second rule: *old elements expire*. The biggest element must be dropped when it becomes too old, and then the next biggest takes over. A stack can only remove at one end. Here elements leave at two: the oldest leaves from the **front**, the ones that cannot win any more leave from the **back**. That is a double-ended queue, a **deque**.

### 1.2 Who can still win?

Suppose `a[i]` arrives and an older element `y` in the window is not bigger than it. Every future window that contains `y` also contains `a[i]`, because windows are contiguous and `a[i]` lies between `y` and the right end of the window. `a[i]` is at least as big, and it expires later than `y`. So `y` can never be a maximum again: throw it away. Do this at every step and what is left is a list of indexes whose values never increase from front to back. The front is the maximum of the window. Each step has four parts:

1. If the front is the index `i - k`, it has just left the window: remove it from the front.
2. While the back is worse than `a[i]`, remove it from the back.
3. Push `i` at the back.
4. Once `i >= k - 1` the first window is complete: the answer is the value at the front.

### 1.3 Cost

Each index is pushed once and removed once, from the front or from the back. The value comparisons are in step 2: each one either removes an index (at most n in all) or ends the loop (once per `i`), so there are at most 2n of them; the test in step 1 compares indexes, not values. Section 3.3 counts them.

---

## 2. Sliding window maximum and minimum

### 2.1 The listing

```cpp
// out[j] is the extreme of a[j..j+k-1]. worse(x, y) says that x is worse than y:
// std::less<>{} gives the maximum of each window, std::greater<>{} the minimum.
template <class T, class Worse>
std::vector<T> slidingExtreme(const std::vector<T>& a, int k, Worse worse) {
    int n = (int)a.size();
    std::vector<T> out;
    if (k <= 0 || k > n) return out;
    out.reserve(n - k + 1);
    std::deque<int> dq;                              // indexes; the values never get better from the front to the back
    for (int i = 0; i < n; ++i) {
        if (!dq.empty() && dq.front() == i - k) dq.pop_front();       // fell out of the window (at most one per step)
        while (!dq.empty() && worse(a[dq.back()], a[i])) dq.pop_back();   // a[i] is later and better: they can never win again
        dq.push_back(i);
        if (i >= k - 1) out.push_back(a[dq.front()]);                 // the front is the winner of the window ending at i
    }
    return out;
}
```

`worse(x, y)` says that `x` is worse than `y`. With `std::less<>{}` an element is worse when it is smaller, which gives the **maximum** of each window; with `std::greater<>{}` it gives the **minimum**. The deque holds *indexes*, not values, because step 1 must know which index leaves, and equal values would not tell. A `k` outside `1..n` returns an empty vector. The tests ran the function on `int`, `double` and `std::string`.

### 2.2 A trace

For `[1, 3, -1, -3, 5, 3, 6, 7]` with `k` = 3:

| i | `a[i]` | front removed (expired) | back removed (beaten) | deque after, index:value | window maximum |
|---|---|---|---|---|---|
| 0 | 1 | none | none | [0:1] | - |
| 1 | 3 | none | 0 | [1:3] | - |
| 2 | -1 | none | none | [1:3, 2:-1] | 3 |
| 3 | -3 | none | none | [1:3, 2:-1, 3:-3] | 3 |
| 4 | 5 | 1 | 3, 2 | [4:5] | 5 |
| 5 | 3 | none | none | [4:5, 5:3] | 5 |
| 6 | 6 | none | 5, 4 | [6:6] | 6 |
| 7 | 7 | none | 6 | [7:7] | 7 |

At `i = 1` the 3 beats the 1. At `i = 3` the deque holds three indexes, the most it can hold. At `i = 4` index 1 (the 3) has just expired (`4 - 3 = 1`) and the 5 beats the other two, so the deque is `[4:5]`. The front is the answer from `i = 2` on.

### 2.3 Details that matter

- **`if`, not `while`, for the expiry.** One index enters per step, so at most one can leave by age.
- **At most `k` indexes.** After the expiry there are at most `k - 1`; pushing makes at most `k`. Section 3.1 uses that.
- **Ties.** The loop removes only elements that are *strictly* worse, so equal values stay and the deque is non-increasing. Removing equal ones too is also correct and keeps the deque strictly decreasing and shorter (Problem E).
- **Where the answer is wrong by one.** Output from `i >= k - 1` (not `k`), and expire the index `i - k` (not `i - k + 1`). Both were checked by planting the mistake: the tests fail.

---

## 3. A ring, blocks, and what they cost

### 3.1 On a ring

The deque never holds more than `k` indexes, so it can live in a fixed ring of that size and never allocate. This is the ring of 05.01 section 3.4: round the capacity up to a power of two and replace the division by a mask. The deque is only pushed and popped at the back and popped at the front, which is simple on a ring.

```cpp
// The same for maxima of ints, on a ring (05.01 section 3.4): the capacity is rounded up to a power of two so that
// a mask replaces the division. The deque never holds more than k indexes, so the ring never overflows.
std::vector<int> slidingMaxRing(const std::vector<int>& a, int k) {
    int n = (int)a.size();
    std::vector<int> out;
    if (k <= 0 || k > n) return out;
    out.reserve(n - k + 1);
    unsigned cap = 1;
    while (cap < (unsigned)k) cap <<= 1;
    std::vector<int> ring(cap);                      // the indexes; the front is ring[head]
    unsigned mask = cap - 1, head = 0, size = 0;
    for (int i = 0; i < n; ++i) {
        if (size > 0 && ring[head] == i - k) { head = (head + 1) & mask; --size; }
        while (size > 0 && a[ring[(head + size - 1) & mask]] < a[i]) --size;   // pop from the back
        ring[(head + size) & mask] = i;              // push at the back
        ++size;
        if (i >= k - 1) out.push_back(a[ring[head]]);
    }
    return out;
}
```

### 3.2 Blocks, without a deque

Cut the array into blocks of `k` consecutive elements. A window of `k` elements covers the end of one block and the start of the next (or exactly one block). So keep, for every position, the maximum from the start of its block up to it (`pre`) and from it to the end of its block (`suf`); then the maximum of the window starting at `i` is `max(suf[i], pre[i + k - 1])`.

```cpp
// Another way, with no deque: cut the array into blocks of k. A window spans at most two blocks:
// the end of one (suffix maxima) and the start of the next (prefix maxima).
std::vector<int> slidingMaxBlocks(const std::vector<int>& a, int k) {
    int n = (int)a.size();
    std::vector<int> out;
    if (k <= 0 || k > n) return out;
    std::vector<int> pre(a), suf(a);                 // pre[i]: maximum from the start of i's block to i; suf[i]: from i to the end of its block
    for (int i = 1; i < n; ++i)
        if (i % k != 0) pre[i] = std::max(pre[i - 1], a[i]);
    for (int i = n - 2; i >= 0; --i)
        if ((i + 1) % k != 0) suf[i] = std::max(suf[i + 1], a[i]);
    out.reserve(n - k + 1);
    for (int i = 0; i + k <= n; ++i)
        out.push_back(std::max(suf[i], pre[i + k - 1]));
    return out;
}
```

For the same array and `k` = 3 the blocks are `[1, 3, -1]`, `[-3, 5, 3]`, `[6, 7]`:

| i | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| `a` | 1 | 3 | -1 | -3 | 5 | 3 | 6 | 7 |
| `pre` | 1 | 3 | 3 | -3 | 5 | 5 | 6 | 7 |
| `suf` | 3 | 3 | -1 | 5 | 5 | 3 | 7 | 7 |

The first window (`i = 0`) is `max(suf[0], pre[2])` = `max(3, 3)` = 3; the window at `i = 2` is `max(suf[2], pre[4])` = `max(-1, 5)` = 5. The work is two passes and one more value per window: about three comparisons per element, and no loop whose length depends on the data. For the minimum, replace `max` by `min`.

### 3.3 What the counts say

Value comparisons for n = 100,000, for the methods that compare values. The *direct scan* looks at all `k` elements of each window. The `multiset` inserts the new element and erases the old one (finding it first). The *lazy heap* is a `priority_queue` of indexes that discards an expired index only when it reaches the top. The last column is the largest size the heap reached.

| Input, `k` | Direct scan | `multiset` | Lazy heap | Blocks | Deque | Heap size at its peak |
|---|---|---|---|---|---|---|
| random, 10 | 899,919 | 908,093 | 1,412,308 | 279,991 | 179,958 | 39,183 |
| random, 1,000 | 98,901,999 | 2,214,598 | 240,871 | 298,801 | 199,790 | 99,443 |
| increasing, 1,000 | 98,901,999 | 2,779,353 | 1,468,946 | 298,801 | 99,999 | 100,000 |
| decreasing, 1,000 | 98,901,999 | 2,828,853 | 1,087,587 | 298,801 | 99,999 | 1,000 |

Four readings. First, the deque never exceeds 2n = 200,000, on any input; on increasing, decreasing and all-equal input it needs just n - 1 = 99,999. Second, the direct scan is hopeless once `k` is large. Third, the lazy heap is the most expensive of all at `k` = 10 and cheap on random data at `k` = 1,000, but on increasing input every push climbs towards the root (1,468,946 comparisons, about 15 per element), and it keeps *expired* entries until they surface: up to 100,000 entries here, where the deque never holds more than `k`. Fourth, a comparison count hides memory traffic and branch behaviour, so look at times too.

### 3.4 What the times say

n = 1,000,000 random values (and, in the second column, the values 0 to n - 1 in increasing order), `k` = 1,000, milliseconds:

| Method | Random (ms) | Increasing (ms) |
|---|---|---|
| Direct scan | 643 | 621 |
| `multiset` | 151 | 61.5 |
| Lazy heap | 23.0 | 31.0 |
| Deque (`std::deque`) | 11.4 | 2.98 |
| Ring, mask | 11.7 | 2.06 |
| Ring, `% k` | 16.1 | 3.77 |
| Blocks | 6.63 | 6.71 |

Across the three runs the deque took 11.4 to 14.8 ms on random data and 2.97 to 3.00 ms on increasing data, and the blocks method 6.33 to 6.67 ms on random data. The ring with a mask is about as fast as `std::deque` on random data and faster on increasing data; the version with `% k` is slower than both, which is the cost of a division. The deque is much faster on sorted data than on random data, probably because the loops that pop from the back are predictable on sorted data and not on random data (I did not measure the cause). The blocks method takes the same time on both and wins on random data. The multiset is the slowest of the practical methods; it is what you need when elements leave in an arbitrary order and not by age.

---

## 4. The deque as a min-queue

In 05.01 section 6.3 the queue with a minimum was made of two min-stacks. A monotonic deque does the same job. Keep the elements that can still become the minimum, each with a ticket (its arrival number). `push` removes from the back every candidate that is larger than the new element, then adds the new one. `pop` removes the oldest element: if the oldest ticket is at the front of the candidates, remove it there; if not, that element had already been beaten and nothing is stored for it.

```cpp
// A queue with an O(1) amortised minimum. It keeps only the elements that can still become the minimum.
template <class T>
class MonoMinQueue {
public:
    void push(const T& x) {
        while (!cand_.empty() && x < cand_.back().second) cand_.pop_back();   // older and larger: never the minimum again
        cand_.push_back({pushed_++, x});
    }
    void pop() {                                     // removes the oldest element; the queue must not be empty
        if (cand_.front().first == popped_) cand_.pop_front();
        ++popped_;
    }
    const T& min() const { return cand_.front().second; }
    long long size() const { return pushed_ - popped_; }
    bool empty() const { return pushed_ == popped_; }
private:
    std::deque<std::pair<long long, T>> cand_;       // (ticket, value); tickets increase, values never decrease
    long long pushed_ = 0, popped_ = 0;              // tickets handed out / tickets removed
};
```

Eight operations on `5, 3, 4, 4`:

| operation | candidates, ticket:value | `min()` | size |
|---|---|---|---|
| push 5 | 0:5 | 5 | 1 |
| push 3 | 1:3 | 3 | 2 |
| push 4 | 1:3, 2:4 | 3 | 3 |
| push 4 | 1:3, 2:4, 3:4 | 3 | 4 |
| pop | 1:3, 2:4, 3:4 | 3 | 3 |
| pop | 2:4, 3:4 | 4 | 2 |
| pop | 3:4 | 4 | 1 |
| push 2 | 4:2 | 2 | 2 |
| pop | 4:2 | 2 | 1 |

The first `pop` removes the 5, which was beaten by the 3 long ago, so the candidates do not change. The second removes the 3, the front, and the minimum becomes 4. Both queues are O(1) amortised. The worst single operation differs: one `pop` of the two-stack queue can pour n elements, while one `push` here can remove n candidates. This version keeps only the candidates (it cannot return the front element; keep a queue next to it if you need that). The sliding window of section 2 is this queue with `pop` called when an element ages out.

---

## 5. Shortest subarray with sum at least K (LeetCode 862)

### 5.1 The problem, and why a window fails

Find the length of the shortest non-empty subarray whose sum is at least `K` (`K >= 1`), or `-1` if there is none. The numbers may be negative; the statement allows up to 100,000 of them and a `K` up to 10^9, as given on the mirror page.

With positive numbers it is a window: grow on the right until the sum reaches `K`, shrink on the left while it still does. The shrinking is safe because dropping a positive number lowers the sum. With a negative number, dropping it *raises* the sum, and growing can lower it, so the window loses its shape. Take `[84, -37, 32, 40, 95]` and `K` = 167. The best is `[32, 40, 95]`, of length 3. The window method reaches the sum 214 at the end, drops the 84 (sum 130, below `K`) and stops: it answers 5. It never tries dropping the -37 as well, which would give 167.

### 5.2 Prefix sums and a deque

Write `P[j]` for the sum of the first `j` numbers (`P[0] = 0`). The sum of `a[i..j-1]` is `P[j] - P[i]`, so the task is: find `i < j` with `P[j] - P[i] >= K` and `j - i` as small as possible. Walk `j` from left to right and keep a deque of candidate starts `i`. Two rules keep it small:

1. **Use the front, then drop it.** If `P[j] - P[front] >= K`, then `front` gives a subarray of length `j - front`. A later `j'` would give a longer subarray from the same start, so `front` is finished: record its length and remove it. Repeat while the new front also works.
2. **Beat the back.** If `P[back] >= P[j]`, then `j` is a better start than `back`: it is later (shorter subarrays) and its prefix is not higher, so every future `j'` has `P[j'] - P[j] >= P[j'] - P[back]`. Remove `back`.

After rule 2 the prefix sums in the deque strictly increase from front to back. That is why rule 1 only has to look at the front: if the smallest prefix does not give a sum of `K`, no other candidate does.

```cpp
// Length of the shortest non-empty subarray with sum >= K (K >= 1), or -1. The elements may be negative.
int shortestSubarray(const std::vector<int>& a, long long K) {
    int n = (int)a.size();
    std::vector<long long> P(n + 1, 0);              // P[j] - P[i] is the sum of a[i..j-1]
    for (int i = 0; i < n; ++i) P[i + 1] = P[i] + a[i];
    std::deque<int> dq;                              // indexes into P; P strictly increases from the front to the back
    int best = n + 1;
    for (int j = 0; j <= n; ++j) {
        while (!dq.empty() && P[j] - P[dq.front()] >= K) {
            best = std::min(best, j - dq.front());   // a later j would only make this front's subarray longer
            dq.pop_front();
        }
        while (!dq.empty() && P[dq.back()] >= P[j]) dq.pop_back();       // j starts shorter subarrays from a lower prefix
        dq.push_back(j);
    }
    return best <= n ? best : -1;
}
```

### 5.3 A trace

For `[84, -37, 32, 40, 95]` and `K` = 167 the prefix sums are `[0, 84, 47, 79, 119, 214]`:

| j | `P[j]` | front used and removed (index, length) | back removed | deque after, index:`P` | best |
|---|---|---|---|---|---|
| 0 | 0 | none | none | [0:0] | - |
| 1 | 84 | none | none | [0:0, 1:84] | - |
| 2 | 47 | none | 1 | [0:0, 2:47] | - |
| 3 | 79 | none | none | [0:0, 2:47, 3:79] | - |
| 4 | 119 | none | none | [0:0, 2:47, 3:79, 4:119] | - |
| 5 | 214 | 0 (length 5); 2 (length 3) | none | [3:79, 4:119, 5:214] | 3 |

At `j = 2` the prefix 47 beats the 84 (it is later and lower), so index 1 is removed from the back. At `j = 5` the sum from index 0 is 214, which is at least 167: length 5 is recorded and index 0 leaves; the next front, index 2, gives 214 - 47 = 167: length 3. Index 3 would give 135, so the loop stops. The answer is 3.

### 5.4 Notes

The prefix sums use `long long`: with 100,000 numbers an `int` prefix sum can overflow as soon as the numbers pass about 21,000, and the pages I read did not give bounds on the numbers. Each `j` is pushed once and each index leaves once, so the pass is O(n). With `>` in place of `>=` in rule 1 a subarray whose sum is exactly `K` is missed (Problem E).

---

## 6. A window with a limit (LeetCode 1438)

Find the length of the longest subarray in which the largest element minus the smallest is at most `limit` (`limit >= 0`). The statement allows up to 100,000 numbers, each up to 10^9 (as given on the mirror page).

Here the window has no fixed size: it grows on the right and shrinks on the left. It is a two-pointer window (see the sliding window tutorial) whose "sum" is the spread, and the spread cannot be updated by adding and subtracting. It can be read off two deques: one for the maximum, one for the minimum. When the spread is too large, move the left end; the front of each deque is removed when its index falls behind `l`. The left end never moves back, because a window that is too wide stays too wide when it grows.

```cpp
// Length of the longest subarray whose maximum minus minimum is at most limit (limit >= 0).
int longestWithLimit(const std::vector<int>& a, int limit) {
    std::deque<int> hi, lo;                          // indexes: values non-increasing in hi, non-decreasing in lo
    int l = 0, best = 0;
    for (int r = 0; r < (int)a.size(); ++r) {
        while (!hi.empty() && a[hi.back()] < a[r]) hi.pop_back();
        hi.push_back(r);
        while (!lo.empty() && a[lo.back()] > a[r]) lo.pop_back();
        lo.push_back(r);
        while ((long long)a[hi.front()] - a[lo.front()] > limit) {   // too wide: move the left end
            ++l;
            if (hi.front() < l) hi.pop_front();
            if (lo.front() < l) lo.pop_front();
        }
        best = std::max(best, r - l + 1);
    }
    return best;
}
```

For `[8, 2, 4, 7]` and `limit` = 4:

| r | `a[r]` | max deque | min deque | `l` | window length | best |
|---|---|---|---|---|---|---|
| 0 | 8 | [0:8] | [0:8] | 0 | 1 | 1 |
| 1 | 2 | [1:2] | [1:2] | 1 | 1 | 1 |
| 2 | 4 | [2:4] | [1:2, 2:4] | 1 | 2 | 2 |
| 3 | 7 | [3:7] | [2:4, 3:7] | 2 | 2 | 2 |

At `r = 1` the spread 8 - 2 = 6 is too big, so `l` moves to 1 and the 8 leaves the max deque. At `r = 3` the spread of `[2, 4, 7]` is 5, so `l` moves to 2 and the 2 leaves the min deque; the window `[4, 7]` has spread 3. The answer is 2. Every index enters and leaves each deque once, and `l` and `r` each move at most n times: O(n). A `multiset` of the window gives the same answer in O(n log n). The spread is computed in `long long` because `max - min` can exceed the range of `int` when the numbers may be negative.

---

## 7. A DP with a window maximum (LeetCode 1696)

Start at index 0 and jump forward at most `k` steps at a time to the last index; each landing adds the value there. Find the largest total. The statement (mirror page): up to 100,000 numbers, each between -10^4 and 10^4, and `k` up to 100,000.

Let `dp[i]` be the best total of a path that ends at `i`. Then `dp[i] = a[i] + max(dp[i-k], ..., dp[i-1])`. Computing the maximum directly costs O(k) per index, O(nk) in all. But the range `i-k..i-1` is a window that moves right by one at every step, which is the sliding window maximum over the array `dp` as it is being built:

```cpp
// Jump from index 0 to the last index, 1 to k steps at a time; the score is the sum of the values landed on.
long long maxJumpScore(const std::vector<int>& a, int k) {
    int n = (int)a.size();
    std::vector<long long> dp(n);                    // dp[i]: best score of a path that ends at i
    std::deque<int> dq;                              // indexes of the last k states; dp non-increasing from the front to the back
    dp[0] = a[0];
    dq.push_back(0);
    for (int i = 1; i < n; ++i) {
        if (dq.front() < i - k) dq.pop_front();      // out of reach (at most one per step)
        dp[i] = a[i] + dp[dq.front()];               // the best state in reach
        while (!dq.empty() && dp[dq.back()] <= dp[i]) dq.pop_back();
        dq.push_back(i);
    }
    return dp[n - 1];
}
```

The deque holds indexes of the last `k` states with non-increasing `dp`. The ties are popped (`<=`): a later state with the same value lasts longer. The front always exists, because state `i - 1` was just pushed and is always in reach. For `[1, -1, -2, 4, -7, 3]` and `k` = 2 the best total is 7:

| i | `a[i]` | best state in reach | `dp[i]` | deque after, index:`dp` |
|---|---|---|---|---|
| 0 | 1 | - | 1 | [0:1] |
| 1 | -1 | 0 | 0 | [0:1, 1:0] |
| 2 | -2 | 0 | -1 | [0:1, 1:0, 2:-1] |
| 3 | 4 | 1 | 4 | [3:4] |
| 4 | -7 | 3 | -3 | [3:4, 4:-3] |
| 5 | 3 | 3 | 7 | [5:7] |

At `i = 3` the best state in reach is index 1 (`dp` = 0), not index 2, and the 4 beats everything, so the deque is `[3:4]`. The path is index 0, 1, 3, 5: `1 - 1 + 4 + 3` = 7. For the stated limits `|dp| <= 100,000 * 10,000 = 10^9`, which fits an `int`, but the listing uses `long long` so that it does not depend on the bounds (the tests also ran 100,000 values of 10^9).

**The pattern.** "A DP whose transition takes a maximum (or minimum) over a window of earlier states" is a deque problem. Two relatives are in the practice list: constrained subsequence sum (the window maximum is clamped at 0) and max value of equation (rewrite the sum so that one part depends only on `i` and the other only on `j`).

---

## 8. Choosing

| You need | Use |
|---|---|
| The maximum or minimum of every window of fixed size | the deque (section 2), or blocks (section 3.2) when the data is random and speed matters |
| The same, with no allocation | the ring (section 3.1) |
| A queue with a minimum, one element at a time | the mono min-queue (section 4), or two min-stacks (05.01) |
| A shortest or longest subarray with a condition on a sum, with negative numbers | prefix sums and a deque (section 5) |
| A variable window with a condition on the spread | two deques and two pointers (section 6) |
| A DP over a window of earlier states | a deque over the `dp` values (section 7) |
| Elements removed in arbitrary order, not by age | `multiset`, or a heap with lazy deletion |

---

## 9. Common mistakes

1. **Storing values instead of indexes.** You cannot tell which element expires, and equal values are ambiguous.
2. **Expiring with the wrong index.** The index that leaves when `i` arrives is `i - k` for a window ending at `i`; for a DP over `i - k .. i - 1` it is `i - k - 1`.
3. **Forgetting the expiry from one of two deques.** In section 6 the min deque keeps an index left of `l`, the spread never shrinks, `l` runs past `r`, and `front()` of an empty deque is undefined behaviour.
4. **Reading `front()` or `back()` of an empty deque.** Check `empty()` first.
5. **Output one window too early or too late.** The first answer is at `i = k - 1`.
6. **A window method on data with negative numbers** (section 5): use prefix sums and the deque.
7. **Overflow.** Prefix sums, spreads and DP totals need `long long` when the inputs are large.
8. **Using a multiset where a deque is enough.** It is O(log n) per step and several times slower here (section 3.4).

---

## 10. Questions to answer out loud

1. Why can the monotonic stack not do a sliding window? What does the deque add? (1.1)
2. Prove that an element beaten by a later, bigger one in the window can be discarded. (1.2)
3. Prove that the deque pass makes at most 2n value comparisons. (1.3)
4. How many indexes can the deque hold, and how do you use that? (2.3, 3.1)
5. Explain the block method and why a window needs only one block boundary. (3.2)
6. Compare the deque, the blocks, the lazy heap and the multiset: time, memory, and which input hurts each. (3.3, 3.4)
7. How does the mono min-queue differ from the two-stack min-queue? (4)
8. Why does the two-pointer window fail for the shortest subarray with sum at least K? What are the two deque rules? (5.1, 5.2)
9. Why does a window with a limit on the spread need two deques, and why does `l` never move back? (6)
10. State the pattern behind jump game VI, and write the recurrence. (7)

---
## 11. Problems

### 11.1 On paper

**Problem A. Trace the window.**
*Where:* paper, using `slidingExtreme` for maxima.
*Goal:* see the front expire and the back get beaten.
*How:* the array is `[4, 2, 12, 3, 8, 1, 5]` and `k` = 3. After each `i` write the index removed from the front, the indexes removed from the back, the deque as index:value, and the answer once there is one.

**Problem B. Prefix sums and two rules.**
*Where:* paper, using `shortestSubarray`.
*Goal:* find the shortest subarray by hand, and see the rules fire.
*How:* the array is `[-3, -3, -3, 1, 0, 4]` and `K` = 5. Write the prefix sums, then for each `j` the indexes used from the front (with the length), the indexes removed from the back, and the deque. Then run the window method of section 5.1 (grow on the right, shrink on the left while the sum is at least `K`) on the same array. What does it answer?

**Problem C. Two deques.**
*Where:* paper, using `longestWithLimit`.
*Goal:* trace both deques and the left end.
*How:* the array is `[10, 1, 2, 4, 7, 2]` and `limit` = 5. For each `r` write both deques, `l` and the window length. What is the answer, and which window is it?

**Problem D. The DP.**
*Where:* paper, using `maxJumpScore`.
*Goal:* fill `dp` with the deque.
*How:* the array is `[10, -5, -2, 4, 0, 3]` and `k` = 3. For each `i` write the state used, `dp[i]` and the deque. Which indexes does the best path visit?

**Problem E. Three small bugs.**
*Where:* paper.
*Goal:* know which changes are harmless and which are not.
*How:* (a) In `slidingExtreme` also remove *equal* elements from the back. Does the output change? How many indexes does the deque hold at most on `[5, 5, 5, 5, 5]` with `k` = 3, in the listing and in the changed version? (b) In `shortestSubarray` change `>= K` to `> K` in the first loop. Find a one-element input where the answer is wrong. (c) In `longestWithLimit` leave out the line that removes expired indexes from the min deque. Run `[1, 2]` with `limit` 0 and say what goes wrong.

### 11.2 In your editor

**Problem 1. LeetCode 239, Sliding Window Maximum.**
*Where:* LeetCode (Hard on the mirror site; up to 100,000 numbers).
*Goal:* the maximum of every window of `k`.
*How:* section 2. Then write the block method of section 3.2 and run the two against each other and against a brute force on random arrays with many equal values. Then change one to compute minima.

**Problem 2. LeetCode 862, Shortest Subarray with Sum at Least K.**
*Where:* LeetCode (Hard on the mirror site; up to 100,000 numbers, `K` up to 10^9).
*Goal:* the length, or `-1`.
*How:* section 5. First write the window method and find the smallest array on which it fails. Then the deque. Test against an O(n^2) version on arrays with negative numbers, and on 100,000 large numbers to catch an `int` prefix sum.

**Problem 3. LeetCode 1438, Longest Continuous Subarray With Absolute Diff Less Than or Equal to Limit.**
*Where:* LeetCode (Medium on the mirror site; up to 100,000 numbers, each up to 10^9).
*Goal:* the length of the longest subarray whose spread is at most `limit`.
*How:* section 6. Write the `multiset` version too and compare the running time on a large random input.

**Problem 4. LeetCode 1696, Jump Game VI.**
*Where:* LeetCode (Medium on the mirror site; up to 100,000 numbers between -10^4 and 10^4).
*Goal:* the best total from the first to the last index with jumps of at most `k`.
*How:* section 7. Write the O(nk) DP first and use it as the test, sweeping `k` from 1 to `n`.

**Problem 5. A min-queue from a deque.**
*Where:* your editor.
*Goal:* `MonoMinQueue` of section 4, then use it.
*How:* write it from the trace. Test it against `std::deque` plus `std::min_element` with random pushes and pops and few distinct values. Then use it for the window minimum of every `k` elements, and check that it matches the `std::greater<>` version of section 2.

**Problem 6. A ring deque.**
*Where:* your editor.
*Goal:* the window maximum with no allocation after the output vector.
*How:* write the ring of section 3.1 from memory. Run it with `k` = 1, `k` = 3 (not a power of two), `k` = `n`, and sizes where `k` is a power of two. Which line would overwrite a live index if the capacity were `k` and the mask were `k - 1` for `k` = 3?

**More practice.** The statements below were not shown on the mirror pages I read, so they are given from memory: check them.

- **LeetCode 1425, Constrained Subsequence Sum** (Hard on the mirror site). Choose a non-empty subsequence in which neighbours in the subsequence are at most `k` apart in the array, with the largest sum. `dp[i] = a[i] + max(0, the best dp in the last k)`: a window maximum clamped at 0; the answer is the largest `dp`.
- **LeetCode 1499, Max Value of Equation** (Hard on the mirror site). For points sorted by `x`, maximise `y_i + y_j + |x_i - x_j|` over `i < j` with `x_j - x_i <= k`. Rewrite it as `(y_j + x_j) + (y_i - x_i)`: a deque over `y_i - x_i`, with the front removed when `x_j - x_i > k`.

---

## Answers

**Problem A.**

| i | `a[i]` | front removed (expired) | back removed (beaten) | deque after, index:value | window maximum |
|---|---|---|---|---|---|
| 0 | 4 | none | none | [0:4] | - |
| 1 | 2 | none | none | [0:4, 1:2] | - |
| 2 | 12 | none | 1, 0 | [2:12] | 12 |
| 3 | 3 | none | none | [2:12, 3:3] | 12 |
| 4 | 8 | none | 3 | [2:12, 4:8] | 12 |
| 5 | 1 | 2 | none | [4:8, 5:1] | 8 |
| 6 | 5 | none | 5 | [4:8, 6:5] | 8 |

The answers are `[12, 12, 12, 8, 8]`. The 12 expires at `i = 5` (`5 - 3 = 2`). The deque holds three indexes at most.

**Problem B.** The prefix sums are `[0, -3, -6, -9, -8, -8, -4]`:

| j | `P[j]` | front used and removed (index, length) | back removed | deque after, index:`P` | best |
|---|---|---|---|---|---|
| 0 | 0 | none | none | [0:0] | - |
| 1 | -3 | none | 0 | [1:-3] | - |
| 2 | -6 | none | 1 | [2:-6] | - |
| 3 | -9 | none | 2 | [3:-9] | - |
| 4 | -8 | none | none | [3:-9, 4:-8] | - |
| 5 | -8 | none | 4 | [3:-9, 5:-8] | - |
| 6 | -4 | 3 (length 3) | none | [5:-8, 6:-4] | 3 |

The answer is 3: the subarray `[1, 0, 4]`, found at `j = 6` from the start 3. Each of the first three prefix sums is lower than all before it and removes them from the back; at `j = 5` the prefix -8 equals the one at index 4, and the `>=` removes the earlier one. The window method answers -1 on this array.

**Problem C.**

| r | `a[r]` | max deque | min deque | `l` | window length | best |
|---|---|---|---|---|---|---|
| 0 | 10 | [0:10] | [0:10] | 0 | 1 | 1 |
| 1 | 1 | [1:1] | [1:1] | 1 | 1 | 1 |
| 2 | 2 | [2:2] | [1:1, 2:2] | 1 | 2 | 2 |
| 3 | 4 | [3:4] | [1:1, 2:2, 3:4] | 1 | 3 | 3 |
| 4 | 7 | [4:7] | [2:2, 3:4, 4:7] | 2 | 3 | 3 |
| 5 | 2 | [4:7, 5:2] | [2:2, 5:2] | 2 | 4 | 4 |

The answer is 4: the window `[2, 4, 7, 2]` at the end (spread 7 - 2 = 5). At `r = 5` the new 2 is not larger than the 7, so the max deque becomes `[4:7, 5:2]`; in the min deque it removes the 7 and the 4 and keeps the equal 2 at index 2, giving `[2:2, 5:2]`.

**Problem D.**

| i | `a[i]` | best state in reach | `dp[i]` | deque after, index:`dp` |
|---|---|---|---|---|
| 0 | 10 | - | 10 | [0:10] |
| 1 | -5 | 0 | 5 | [0:10, 1:5] |
| 2 | -2 | 0 | 8 | [0:10, 2:8] |
| 3 | 4 | 0 | 14 | [3:14] |
| 4 | 0 | 3 | 14 | [4:14] |
| 5 | 3 | 4 | 17 | [5:17] |

The answer is 17, and two paths give it: 0, 3, 5 (`10 + 4 + 3`) and 0, 3, 4, 5 (the 0 at index 4 costs nothing). At `i = 4` the values 14 and 14 tie, and the later one stays in the deque because it lasts longer.

**Problem E.** (a) The output does not change. On `[5, 5, 5, 5, 5]` with `k` = 3 the listing keeps all the equal indexes and holds 3 at its largest; the changed version removes the earlier equal one at every step and holds 1. (b) `[3]` with `K` = 3: the sum is exactly `K`, the test `3 > 3` fails, and the function answers `-1` instead of 1. (c) For `[1, 2]` with `limit` 0 the spread is 1. The max deque loses its index when `l` moves, but the min deque keeps the index 0, the spread never shrinks, `l` goes past `r`, and `hi.front()` is read from an empty deque: undefined behaviour (the sanitizer stops the program).

**Problem 1.** The block method must agree with the deque on every input; use lengths that are not multiples of `k`, so that the last block is short. The minimum version needs `std::greater<>` and `min` in the blocks.

**Problem 2.** The shortest array I found on which the window method fails (searching every array with numbers from -3 to 4 and `K` from 1 to 6, up to length 5) is `[-3, 1]` with `K` = 1: the window method answers -1, the right answer is 1. The longer example of section 5.1 is `[84, -37, 32, 40, 95]` with `K` = 167. For the big test use 100,000 values of 10^9 and `K` = 5 * 10^9: the answer is 5, and an `int` prefix sum overflows.

**Problem 3.** The `multiset` version keeps the elements of the window and reads `*rbegin() - *begin()`; it is O(n log n). Measure both on a large random input and see which wins.

**Problem 4.** The O(nk) DP is `dp[i] = a[i] + max(dp[i-k..i-1])`. With `k` = 1 the answer is the sum of all the numbers; with `k` at least `n - 1` it is the first plus the last plus every positive number between them.

**Problem 5.** The tickets can be plain counters. For the window minimum, push each `a[i]`, and once `i >= k - 1` read `min()` and then `pop()`. The result equals `slidingExtreme(a, k, std::greater<>{})`.

**Problem 6.** The line is `ring[(head + size) & mask] = i;`. With capacity 3 and mask 2 the positions 0, 1, 2, 3 map to the slots 0, 0, 2, 2: position 1 lands on the slot of position 0. A mask only works when the capacity is a power of two (05.01 section 3.4), so the capacity must be rounded up to a power of two that is at least `k`.

---

## Cheat sheet

- **Window maximum:** a deque of indexes, values non-increasing from front to back. Per step: expire the front if it is `i - k`; remove from the back while worse than `a[i]`; push `i`; answer at `i >= k - 1` is the front. At most `2n` comparisons, at most `k` indexes.
- **Minimum:** the same with the comparison reversed (`std::greater<>`).
- **Ring:** capacity a power of two at least `k`, mask for the index.
- **Blocks:** `pre` and `suf` inside blocks of `k`; the window maximum is `max(suf[i], pre[i + k - 1])`.
- **Min-queue:** deque of (ticket, value); `push` removes larger values from the back; `pop` removes the front only if its ticket is the oldest.
- **Shortest subarray with sum at least K:** prefix sums in `long long`; while `P[j] - P[front] >= K`, record and pop the front; while `P[back] >= P[j]`, pop the back; push `j`.
- **Spread limit:** two deques, `l` only moves right.
- **Window DP:** `dp[i] = a[i] + best of the last k dp`; deque of `dp` indexes.
- **Not a deque job:** elements that leave in an arbitrary order (use a `multiset`).

---

## Sources

- Problem statements and labels, from mirror sites because LeetCode itself could not be opened from here: [239](https://brain-leetcode.pages.dev/02xx/239/) (Hard; up to 100,000 numbers, value range not stated on the page), [862](https://brain-leetcode.pages.dev/08xx/862/) (Hard; up to 100,000 numbers, `K` up to 10^9, `-1` if none), [1438](https://brain-leetcode.pages.dev/14xx/1438/) (Medium; up to 100,000 numbers, values and limit up to 10^9), [1696](https://leetcode.doocs.org/en/lc/1696/) (Medium; up to 100,000 numbers between -10^4 and 10^4, `k` up to 100,000), [1425](https://brain-leetcode.pages.dev/14xx/1425/) (Hard; no statement on the page) and [1499](https://lyte-code.pages.dev/problems/max-value-of-equation) (Hard; no statement on the page).
- cp-algorithms, [Minimum Stack / Minimum Queue](https://cp-algorithms.com/data_structures/stack_queue_modification.html): the min-queue constructions and the minimum of every fixed-length subarray in linear time.
- Daniel Lemire, [Streaming Maximum-Minimum Filter Using No More than Three Comparisons per Element](https://arxiv.org/abs/cs/0610046): running maximum and minimum together in at most three comparisons per element.
- Alain Frisch, [Sliding max](https://www.lexifi.com/blog/ocaml/sliding_max/) (LexiFi blog): a block-based sliding maximum with left and right folds inside blocks (blocks of length `k + 1`), a relative of section 3.2.
- The comparison counts of section 3.3 and the times of section 3.4 come from the tutorial's own test and benchmark programs.

---

**Next:** tutorial 04, expression evaluation and parsing (balanced brackets, reverse Polish notation, the calculator problems, decode string, simplify path, shunting-yard). Say "next" when you are ready.
