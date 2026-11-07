# DSA 05.02: The Monotonic Stack

*Part of the [Stacks and queues plan](00_plan.md) · Time: about 6 hours (3 reading, 3 problems) · You need: [Stack and queue fundamentals](01_stack_and_queue_fundamentals.md) (the stack as a `std::vector`, and the amortised argument in section 5.2 of that tutorial) · Related: [Two pointers](../01_arrays_and_strings/02_two_pointers.md) (the two-pointer answer to trapping rain water) · No companion file: every listing is in this tutorial.*

By the end of this tutorial you can:

- explain the one idea behind the monotonic stack (a waiting room that stays sorted) and why a whole pass costs at most 2n comparisons;
- write next greater, next smaller, previous greater and previous smaller from one template, and choose the right tie rule;
- solve daily temperatures, the circular variant and the online stock span, and say when the stack should hold indexes and when values;
- solve the largest rectangle in a histogram and trapping rain water with the same stack, and say how they differ;
- turn "sum over all subarrays" into "sum over elements" with contribution counting, and get the ties right;
- use the stack as a greedy tool (remove K digits).

**Where the numbers come from.** The only measurements here are *counts of comparisons*, made through a small wrapper type that counts every `<`. A count, unlike a time, is the same on every machine. Every listing was compiled with `-Wall -Wextra` and checked against a brute force on all small arrays and on random larger ones: 963,435 checks passed under AddressSanitizer with UBSan (GCC 13.3), and the same program passed at `-O2` with GCC and with clang 18. Problem labels (Easy, Medium, Hard) come from mirror sites of the problem statements, because LeetCode itself could not be opened from here; the Sources list says which problem has no label.

All listings need these headers: `<algorithm>`, `<string>`, `<utility>` and `<vector>`.

---

## 1. The idea

### 1.1 The question

Many problems ask, for every element of an array, about its nearest bigger or smaller neighbour: how many days until it is warmer, how far a bar can stretch before a shorter bar stops it, how many earlier prices it beats. The general problem is called *all nearest smaller values* (or greater).

Take `a = [2, 1, 2, 4, 3]` and ask for the first *strictly greater* element to the right of each one. Element 0 (value 2) finds the 4 at index 3; element 1 (value 1) finds the 2 at index 2; element 2 finds the 4 at index 3; the 4 and the 3 find nothing. As indexes, with `n` = 5 standing for "none": `[3, 2, 3, 5, 5]`.

The direct way scans to the right from every element until it meets a greater one. That is fine on random data and terrible on some other data. This table counts the `<` comparisons for n = 20,000 (random values below 1,000,000 from `std::mt19937` with seed 5), for the direct scan and for the stack of section 2:

| Input (n = 20,000) | Scan right from every element | The stack of section 2 |
|---|---|---|
| random | 201,641 | 39,980 |
| increasing | 19,999 | 19,999 |
| decreasing | 199,990,000 | 19,999 |
| all equal | 199,990,000 | 19,999 |

Read it twice. A test on random data would not have noticed anything wrong with the direct scan: it stops early because a bigger value turns up quickly. On a decreasing or an all-equal input every scan runs to the end and the count is n(n - 1) / 2 = 199,990,000. The stack never goes above 2n = 40,000, on any input.

### 1.2 The waiting room

Walk through the array from left to right and keep a stack of the indexes that have **not yet found their answer**. Call it the waiting room. When `a[i]` arrives:

1. Every waiting element that is smaller than `a[i]` has just found its answer: it is `i`. Take it out.
2. Then `i` itself joins the waiting room, because its answer is still to come.

Why can a *stack* do step 1 cheaply? Look at the waiting room from bottom to top and suppose it ever had a smaller value `u` *below* a bigger value `v`. Then `v` would have taken `u` out when `v` arrived. So the values in the waiting room never increase from bottom to top: it is *sorted*, and that is what **monotonic** means here. The elements that `a[i]` can answer are therefore a block at the top. Pop until you meet one that is not smaller; everything below it is not smaller either, because going down the values only get bigger or stay equal. You never need to look further.

### 1.3 Why a pass is O(n)

Each index is pushed once and popped at most once. Every comparison in the inner loop either leads to a pop (at most n of those in the whole run) or ends the loop (one per `i`, so n of those). That is at most 2n comparisons. One `i` can pop many elements, but each of those pops was paid for when its element was pushed; this is the coin argument of 05.01 section 5.2 again. The table agrees: the largest count in the stack column is 39,980, below 40,000.

---

## 2. Next greater element

### 2.1 The listing

```cpp
// For each i: the index of the first element to the right that is strictly greater, or n if none.
template <class T>
std::vector<int> nextGreater(const std::vector<T>& a) {
    int n = (int)a.size();
    std::vector<int> next(n, n);
    std::vector<int> st;                    // indexes still waiting for an answer
    for (int i = 0; i < n; ++i) {
        while (!st.empty() && a[st.back()] < a[i]) {
            next[st.back()] = i;            // a[i] is the first bigger element to the right of it
            st.pop_back();
        }
        st.push_back(i);                    // values on the stack never increase from bottom to top
    }
    return next;
}
```

`n` in the answer means "none". The function only uses `<`, so it works for any type that has one; the tests ran it on `int`, `double` and `std::string`. Whatever is left on the stack at the end never found a greater element, and its answer stays `n`.

### 2.2 A trace

For `[2, 1, 2, 4, 3]`:

| i | `a[i]` | indexes answered (their answer is `i`) | stack after, as index:value |
|---|---|---|---|
| 0 | 2 | none | [0:2] |
| 1 | 1 | none | [0:2, 1:1] |
| 2 | 2 | 1 | [0:2, 2:2] |
| 3 | 4 | 2, 0 | [3:4] |
| 4 | 3 | none | [3:4, 4:3] |

At `i = 2` the value 2 takes out the 1 (index 1) but not the 2 at index 0: "strictly greater" means an equal value does not answer anyone. So the waiting room holds `[0:2, 2:2]`, two equal values side by side. It is non-increasing, not strictly decreasing. At `i = 3` the 4 answers both of them at once, which is why the answer for index 0 is 3 and not 2.

### 2.3 Four flavours, and both sides in one pass

Changing the comparison changes the question. The stack always holds what is still waiting:

| You want | Pop while | The waiting room, bottom to top |
|---|---|---|
| next strictly greater | `a[top] < a[i]` | non-increasing |
| next greater or equal | `a[top] <= a[i]` | strictly decreasing |
| next strictly smaller | `a[top] > a[i]` | non-decreasing |
| next smaller or equal | `a[top] >= a[i]` | strictly increasing |

There is a second answer hiding in the same pass. After the pops, the index left *under* `i` is the nearest index to the left that `i` did not pop. With the rule `a[top] < a[i]` that is the nearest element on the left that is greater **or equal**; with the rule `a[top] <= a[i]` it is the nearest that is **strictly** greater. So one pass gives the next answer on the right and the previous answer on the left, always with *opposite* strictness. Section 6 needs exactly that, and it is the source of most tie bugs:

```cpp
struct Neighbours {
    std::vector<int> prev, next;            // -1 and n mean "none"
};

// popIf(top, x) says whether the element on top of the stack must leave when x arrives.
template <class PopIf>
Neighbours neighbours(const std::vector<int>& a, PopIf popIf) {
    int n = (int)a.size();
    Neighbours r{std::vector<int>(n, -1), std::vector<int>(n, n)};
    std::vector<int> st;
    for (int i = 0; i < n; ++i) {
        while (!st.empty() && popIf(a[st.back()], a[i])) {
            r.next[st.back()] = i;          // the popped element's answer to the right
            st.pop_back();
        }
        if (!st.empty()) r.prev[i] = st.back();     // what is left under i is its answer to the left
        st.push_back(i);
    }
    return r;
}
```

The caller passes the pop rule: `popIf(top, x)` says whether the value `top` on the stack must leave when `x` arrives. The tests compared all four rules in the table with a brute force on every array of length up to 7 with values 0 to 2, and on random arrays.

---

## 3. Distances, circles and streams

### 3.1 Daily temperatures (LeetCode 739)

Given the temperature of each day, report for each day how many days you must wait for a warmer one, or 0 if there is none. It is the same loop; the answer is a *distance*, so we store `i - st.back()` instead of `i`:

```cpp
std::vector<int> dailyTemperatures(const std::vector<int>& t) {
    std::vector<int> wait(t.size(), 0), st;
    for (int i = 0; i < (int)t.size(); ++i) {
        while (!st.empty() && t[st.back()] < t[i]) {
            wait[st.back()] = i - st.back();        // days until a warmer one
            st.pop_back();
        }
        st.push_back(i);
    }
    return wait;
}
```

The statement's example `[73, 74, 75, 71, 69, 72, 76, 73]` gives `[1, 1, 4, 2, 1, 1, 0, 0]`. A distance needs positions, so this stack holds indexes.

### 3.2 A circular array (LeetCode 503)

Now the array is a circle: after the last element, the search continues at the first. Walk `2n` steps with `i % n`. Only the first lap pushes; the second lap only answers.

```cpp
// For each element: the next strictly greater value going around the circle, or -1.
std::vector<int> nextGreaterCircular(const std::vector<int>& a) {
    int n = (int)a.size();
    std::vector<int> res(n, -1), st;
    for (int i = 0; i < 2 * n; ++i) {               // two laps; only the first lap pushes
        int x = a[i % n];
        while (!st.empty() && a[st.back()] < x) {
            res[st.back()] = x;
            st.pop_back();
        }
        if (i < n) st.push_back(i);
    }
    return res;
}
```

Why not push on the second lap too? An index pushed there could only be answered by elements to its right on that same lap, and the first lap has already looked at exactly those elements on behalf of the same index. The extra pushes would add work and no answers. (I tried the variant that pushes on both laps: the results are identical, so no test can tell the two apart, but it uses up to twice the stack.) For `[1, 2, 1]` the answer is `[2, -1, 2]`: the last 1 goes round the circle and meets the 2. The maximum never has a strictly greater element, so every copy of the maximum gets `-1`.

### 3.3 Online stock span (LeetCode 901)

Prices arrive one day at a time. The **span** of today is the number of consecutive days, counting today and going backwards, on which the price was at most today's price. That is `i - (previous strictly greater index)`, but we cannot see the future and we do not want to keep every day. Keep pairs instead: `(price, span)`, where the span says how many days that price already covers. When today's price pops a pair, today takes over its days.

```cpp
class StockSpanner {
public:
    int next(int price) {
        int span = 1;                               // today counts
        while (!st_.empty() && st_.back().first <= price) {
            span += st_.back().second;              // swallow the days that the popped price covered
            st_.pop_back();
        }
        st_.push_back({price, span});
        return span;
    }
private:
    std::vector<std::pair<int, int>> st_;           // (price, span); prices strictly decrease upwards
};
```

The prices 100, 80, 60, 70, 60, 75, 85 give the spans 1, 1, 1, 2, 1, 4, 6. The 75 pops the 60 (span 1) and the 70 (span 2), so its span is 1 + 1 + 2 = 4; the 85 pops the 75 (span 4) and the 80 (span 1), so it is 1 + 4 + 1 = 6. The pairs on the stack have strictly decreasing prices, and in the worst case (prices only ever fall) the stack holds one pair per day.

### 3.4 Indexes or values?

| The stack holds | Use it when | Seen in |
|---|---|---|
| indexes | you need a distance, a width, or to write the answer into the slot of the waiting element | 739, 503, the histogram |
| `(value, count)` pairs | the data is a stream and the past is not kept, or the count already is the answer | 901 |

An index gives you both the position and, through the array, the value. If the array is at hand and positions matter, use indexes.

---

## 4. The largest rectangle in a histogram (LeetCode 84)

### 4.1 The idea

A histogram has bars of width 1 and heights `h[0..n-1]`. Find the area of the largest rectangle that fits inside it. Whatever the best rectangle is, its height equals the height of its shortest bar. So ask the question once per bar: *how wide can a rectangle of height `h[i]` be?* It stretches left until a shorter bar and right until a shorter bar, so

`width = (index of the next smaller on the right) - (index of the previous smaller on the left) - 1`, and `area = h[i] * width`.

That is two nearest-smaller lookups per bar, and one stack pass gives both: when bar `i` pops the bar on top, `i` is its right wall, and the bar *under* it on the stack is its left wall.

### 4.2 The listing

```cpp
long long largestRectangle(const std::vector<int>& h) {
    int n = (int)h.size();
    std::vector<int> st;                            // indexes of bars with strictly increasing heights
    long long best = 0;
    for (int i = 0; i <= n; ++i) {
        int cur = (i == n) ? 0 : h[i];              // a bar of height 0 after the end pops everything
        while (!st.empty() && h[st.back()] >= cur) {
            int height = h[st.back()];
            st.pop_back();
            int left = st.empty() ? -1 : st.back(); // the bar under it is the nearest smaller on the left
            best = std::max(best, (long long)height * (i - left - 1));
        }
        st.push_back(i);
    }
    return best;
}
```

Two details. The loop runs one step past the end with a bar of height 0, a **sentinel** that pops everything still waiting; without it the bars that never meet a shorter bar to their right would never be measured. And the pop rule is `>=`: a bar of equal height is popped too. The area computed for the earlier of two equal bars is smaller than the true one, but it is still a real rectangle, and the *last* bar of a run of equal heights computes the full width (its left wall is the bar below the whole run). For `[2, 2, 2]` the areas are 2, then 4, then 6. The maximum is what we return.

### 4.3 A trace

For `[2, 1, 5, 6, 2, 3]`, the answer is 10 (the bars of heights 5 and 6 give a 5-high rectangle two bars wide):

| i | bar | pops: height, left wall, width, area | best | stack after, as index:height |
|---|---|---|---|---|
| 0 | 2 | none | 0 | [0:2] |
| 1 | 1 | h=2, left=-1, width=1, area=2 | 2 | [1:1] |
| 2 | 5 | none | 2 | [1:1, 2:5] |
| 3 | 6 | none | 2 | [1:1, 2:5, 3:6] |
| 4 | 2 | h=6, left=2, width=1, area=6; h=5, left=1, width=2, area=10 | 10 | [1:1, 4:2] |
| 5 | 3 | none | 10 | [1:1, 4:2, 5:3] |
| 6 | 0 (end) | h=3, left=4, width=1, area=3; h=2, left=1, width=4, area=8; h=1, left=-1, width=6, area=6 | 10 | [] |

The line for `i = 4` shows the two kinds of pops: the 6 is measured with width 1, then the 5 with width 2 (its left wall is the 1 at index 1, its right wall is the 2 arriving at index 4). At the end the sentinel measures the rest, down to the 1, which spans all six bars (area 6).

### 4.4 Overflow

An area is a height times a width. With n bars of height up to H the area can reach n * H, and the example the tests used is `h[i] = i` for `i` below 100,000: the best rectangle is 50,000 high and 50,000 wide, an area of 2,500,000,000. That is larger than the biggest 32-bit `int` (2,147,483,647), so the listing computes the area in `long long`. (The statement of the LeetCode problem did not list limits on the pages I read, so I do not know whether its own inputs reach that size. The habit is cheap; keep it.)

---

## 5. Trapping rain water (LeetCode 42)

Given an elevation map (bar heights, width 1), how much water stays on it after rain? The two-pointer answer (Problem 6 of the [Two pointers](../01_arrays_and_strings/02_two_pointers.md) tutorial) adds up column by column and needs no extra memory. The stack answer fills the water **layer by layer** instead, and it is worth knowing because it is the histogram idea upside down: the histogram looks for the nearest *smaller* walls, the rain for the nearest *greater*.

Keep the bars in a stack with non-increasing heights. When bar `i` is taller than the bar on top, the top bar is the bottom of a pit. Its **right wall** is `i`. Its **left wall** is the bar under it on the stack (that bar is at least as tall as the pit's bottom). The pit holds one layer of water:

`width = i - left - 1` and `depth = min(h[left], h[i]) - h[bottom]`.

If nothing is under the popped bar, there is no left wall and the water runs off the edge.

```cpp
long long trap(const std::vector<int>& h) {
    long long water = 0;
    std::vector<int> st;                            // indexes of bars with non-increasing heights
    for (int i = 0; i < (int)h.size(); ++i) {
        while (!st.empty() && h[st.back()] < h[i]) {
            int bottom = h[st.back()];
            st.pop_back();
            if (st.empty()) break;                  // no wall on the left: the water runs off
            int width = i - st.back() - 1;
            int depth = std::min(h[st.back()], h[i]) - bottom;
            water += (long long)width * depth;      // one horizontal layer of water
        }
        st.push_back(i);
    }
    return water;
}
```

The classic example `[0, 1, 0, 2, 1, 0, 1, 3, 2, 1, 2, 1]` holds 6 units. The layers, in the order they are found:

| i | bottom (index:height) | left wall (index:height) | width | depth | layer | total so far |
|---|---|---|---|---|---|---|
| 3 | 2:0 | 1:1 | 1 | 1 | 1 | 1 |
| 6 | 5:0 | 4:1 | 1 | 1 | 1 | 2 |
| 7 | 6:1 | 4:1 | 2 | 0 | 0 | 2 |
| 7 | 4:1 | 3:2 | 3 | 1 | 3 | 5 |
| 10 | 9:1 | 8:2 | 1 | 1 | 1 | 6 |

The row with depth 0 comes from two neighbours of equal height (the bottom 1 at index 6 and the wall 1 at index 4): nothing to add, but the pop is still needed so that the next wall can be found. The row with width 3 is the lake over indexes 4 to 6 between the walls at indexes 3 and 7; the small pit at index 5 had been filled one layer earlier at `i = 6`, so this layer sits on top of it.

---

## 6. Counting contributions: sum of subarray minimums (LeetCode 907)

Sum the minimum of every contiguous subarray of `a`, modulo 1,000,000,007. An array of length n has n(n + 1) / 2 subarrays, so listing them all takes O(n^2) at least, and the statement allows n up to 30,000 (as stated on the mirror page).

**Flip the sum.** Instead of "for each subarray, find its minimum", ask "for each element, in how many subarrays is it the minimum?". If `a[i]` is the minimum of the subarray from `l` to `r`, then `l` lies after the previous smaller element and `r` before the next smaller one. With `prev` and `next` as those two indexes (`-1` and `n` when absent),

`count(i) = (i - prev) * (next - i)` and the answer is the sum of `a[i] * count(i)`.

The two lookups are the pair of answers from one pass, which is exactly what `neighbours` gives.

**Ties are the whole difficulty.** Take `[2, 2]`: the subarrays are `[2]`, `[2]` and `[2, 2]`, with minimum 2 each, so the sum is 6. If both elements counted the subarray `[2, 2]` as theirs (both sides strict), the sum would be 8. If neither did (both sides non-strict), it would be 4. A subarray must be claimed by *exactly one* of its minimums. Make one side strict and the other not: here, **next strictly smaller** on the right and **previous smaller or equal** on the left. An equal element on the left blocks the stretch; an equal element on the right does not; so each subarray belongs to its *leftmost* minimum. That is the pair of opposite strictnesses from section 2.3.

```cpp
int sumSubarrayMins(const std::vector<int>& a) {
    const long long MOD = 1'000'000'007;
    int n = (int)a.size();
    // next strictly smaller to the right, previous smaller-or-equal to the left
    Neighbours nb = neighbours(a, [](int top, int x) { return top > x; });
    long long total = 0;
    for (int i = 0; i < n; ++i) {
        long long count = (long long)(i - nb.prev[i]) * (nb.next[i] - i);   // subarrays whose minimum is a[i]
        total = (total + a[i] % MOD * (count % MOD)) % MOD;
    }
    return (int)total;
}
```

The rule `top > x` pops while the top is strictly greater, so what it pops is answered by the next strictly smaller, and what is left under is smaller or equal. The count fits in an `int` for n up to 30,000, but its product with `a[i]` does not, so the multiplication is in `long long` and reduced at every step. For `[3, 1, 2, 4]` (sum 17):

| i | `a[i]` | prev | next | count | contribution |
|---|---|---|---|---|---|
| 0 | 3 | -1 | 1 | 1 | 3 |
| 1 | 1 | -1 | 4 | 6 | 6 |
| 2 | 2 | 1 | 4 | 2 | 4 |
| 3 | 4 | 2 | 4 | 1 | 4 |

The mirror choice (previous strictly smaller, next smaller or equal, so each subarray goes to its *rightmost* minimum) gives the same sum, and the tests checked the answers against a brute force over all subarrays. The same flip works for sums of subarray maximums, and the sum of subarray ranges (largest minus smallest) is the maximums sum minus this one.

---

## 7. A greedy stack: remove K digits (LeetCode 402)

Given a string of digits `num` and a number `k`, remove exactly `k` digits so that the number left is as small as possible. The result must not have leading zeros, and an empty result is written `"0"`.

Compare two numbers of the same length digit by digit from the left: the first difference decides. Scan the digits left to right. If the new digit `c` is smaller than the last digit we kept, then dropping that last digit makes the number smaller at that position, and nothing later can do better there. So pop the last kept digit while it is larger than `c` (and removals remain), then push `c`. The kept digits are non-decreasing: a monotonic stack again, with at most `k` pops. If removals are left over at the end, the digits are in non-decreasing order, so the biggest are at the end: cut from the end.

```cpp
std::string removeKDigits(const std::string& num, int k) {
    std::string st;                                 // the digits kept so far
    for (char c : num) {
        while (k > 0 && !st.empty() && st.back() > c) {
            st.pop_back();                          // a bigger digit before a smaller one is worth dropping
            --k;
        }
        st.push_back(c);
    }
    st.resize(st.size() - k);                       // deletions left over come off the end
    std::size_t p = st.find_first_not_of('0');
    return p == std::string::npos ? "0" : st.substr(p);
}
```

For `1432219` with `k` = 3:

| digit | popped | stack after | `k` left |
|---|---|---|---|
| 1 | 0 | `1` | 3 |
| 4 | 0 | `14` | 3 |
| 3 | 1 | `13` | 2 |
| 2 | 1 | `12` | 1 |
| 2 | 0 | `122` | 1 |
| 1 | 1 | `121` | 0 |
| 9 | 0 | `1219` | 0 |

The answer is `1219`. Two edge cases. For `"10200"` with `k` = 1 the stack ends as `0200`, and stripping the leading zeros gives `200`. For `"10"` with `k` = 2 everything is removed and the answer is `"0"`. The tests compared the function with a brute force over all subsets of digits for every string of length up to 7 over the digits 0 to 3 and every `k`.

---

## 8. Recognising the pattern

| The problem says | Think |
|---|---|
| "next / previous greater / smaller", "how many days until", "how far to the left" | the template of section 2 |
| "the widest / largest range in which this element is the minimum / maximum" | the histogram idea (section 4) |
| "the sum (or count) over all subarrays of their minimum / maximum" | contribution counting (section 6) |
| "remove k to make the smallest / largest" | the greedy stack (section 7) |
| "a window of size k" | the monotonic *deque*, tutorial 03 |
| "build a Cartesian tree in O(n)" | the same stack (not covered here) |

If a problem has the shape "for every element, the nearest one that is not like it", try the stack first.

---

## 9. Common mistakes

1. **The wrong tie rule.** `<` against `<=` in the pop changes the answer for equal values. Decide first whether an equal value should answer, or block, and check on `[2, 2]`.
2. **Both sides strict (or both non-strict) in contribution counting.** A subarray with two equal minimums is counted twice or never.
3. **Storing values when you need positions.** Distances and widths need indexes.
4. **Reading `st.back()` on an empty stack.** Put `!st.empty()` first in the loop condition.
5. **Forgetting what is left at the end.** Whatever remains was never answered: the default `n` or `-1`, or, in the histogram, the sentinel bar that measures it.
6. **Overflow.** Areas (n * H) and counts times values need `long long`.
7. **Pushing on both laps** in the circular version (wasted work), or walking only one lap (misses the wrap-around).
8. **Mixing `int` and `std::size_t`.** `i - 1` or a `-1` stored in an unsigned type becomes a huge number. The listings use `int` indexes for this reason.

---

## 10. Questions to answer out loud

1. State the invariant of the waiting room. Why does it let you stop popping at the first element that does not leave? (1.2)
2. Prove that a pass costs at most 2n comparisons. What input gives exactly n - 1? (1.3)
3. What do you change to get the next smaller element? The previous greater? (2.3)
4. What is the stack in the stock span, and why is it not a stack of indexes? (3.3, 3.4)
5. Explain the sentinel in the histogram. What if you leave it out? (4.2)
6. How does the left wall of a popped bar come out of the stack for free? (4.1)
7. In trapping rain water, why does the water layer use `min(h[left], h[i]) - h[bottom]`? (5)
8. Why must exactly one side of the sum-of-minimums be strict? Show it on `[2, 2]`. (6)
9. Why does the remove-K-digits greedy pop only while a larger digit sits before a smaller one? (7)
10. When is this the wrong tool, and what do you use for a fixed-size window? (8)

---
## 11. Problems

### 11.1 On paper

**Problem A. Trace the template.**
*Where:* paper.
*Goal:* see the waiting room fill and empty.
*How:* run `nextGreater` on `[5, 3, 8, 3, 6, 2, 7]`. After each `i` write the indexes answered and the stack as index:value. What is the final answer vector, and which indexes are still on the stack at the end?

**Problem B. A histogram with a run of equal bars.**
*Where:* paper, using `largestRectangle`.
*Goal:* see what the `>=` pop does to equal heights.
*How:* the heights are `[3, 1, 3, 2, 2]`. For each pop write the height, the left wall, the width and the area. Which pop measures the best rectangle? Which pop measures a rectangle that is real but too narrow, and why does that do no harm?

**Problem C. Water in layers.**
*Where:* paper, using `trap`.
*Goal:* find the layers by hand.
*How:* the map is `[3, 0, 2, 0, 4]`. For each pop write the bottom, the left wall, the width, the depth and the layer. Check the total against the column-by-column rule "water above a bar = min(tallest bar to its left, tallest to its right) minus its height".

**Problem D. Counting contributions.**
*Where:* paper, using `sumSubarrayMins`.
*Goal:* fill the count table.
*How:* the array is `[11, 81, 94, 43, 3]`. For each index write `prev`, `next`, the count and the contribution. Then say which subarrays the element 43 is the minimum of.

**Problem E. Three tie bugs.**
*Where:* paper.
*Goal:* find what a wrong comparison breaks, and what it does not.
*How:* (a) In `StockSpanner` change `<=` to `<`. Find the shortest price sequence that gives a wrong span. (b) In `largestRectangle` change `>=` to `>`. Is the result still right? Try `[2, 2, 2]` and explain. (c) In `sumSubarrayMins` make *both* sides strict. What does `[2, 2]` give, and what if both sides are non-strict?

### 11.2 In your editor

**Problem 1. LeetCode 496, Next Greater Element I.**
*Where:* LeetCode (Easy on the mirror site).
*Goal:* for each number in `nums1`, the next greater number to its right in `nums2`, or `-1`. Every number of `nums1` appears in `nums2`, and the values within each array are unique.
*How:* run section 2 over `nums2` and keep a map from value to its answer, then look up each number of `nums1`. The follow-up on the page asks for O(m + n) time: say why this is.

**Problem 2. LeetCode 503, Next Greater Element II.**
*Where:* LeetCode (Medium on the mirror site).
*Goal:* the same on a circular array.
*How:* section 3.2: two laps, push on the first only. Write the variant that pushes on both laps and test that the two agree on random arrays; what does the second one cost?

**Problem 3. LeetCode 739, Daily Temperatures.**
*Where:* LeetCode (the pages I read did not show a label).
*Goal:* days to wait for a warmer day.
*How:* section 3.1. The page says the temperatures are whole numbers from 30 to 100; use that for a second solution that goes from the right and finds the next warmer day by *jumping* with the answers already known. Does it stay linear?

**Problem 4. LeetCode 901, Online Stock Span.**
*Where:* LeetCode (Medium on the mirror site; at most 10,000 calls).
*Goal:* `next(price)` returns the span.
*How:* section 3.3. Test it against a brute force that keeps every price, including long runs of equal prices.

**Problem 5. LeetCode 84, Largest Rectangle in Histogram.**
*Where:* LeetCode (Hard on the mirror site).
*Goal:* the area of the largest rectangle.
*How:* section 4, from memory. Test against an O(n^2) version on all arrays up to length 7 with small heights, and on the staircase `0, 1, 2, ..., 99999` (what area do you expect, and does an `int` hold it?).

**Problem 6. LeetCode 42, Trapping Rain Water.**
*Where:* LeetCode (Hard on the mirror site).
*Goal:* the trapped water.
*How:* write the stack version of section 5, then the two-pointer version of the other tutorial, and run them against each other on random maps. Which uses less memory?

**Problem 7. LeetCode 907, Sum of Subarray Minimums.**
*Where:* LeetCode (Medium on the mirror site).
*Goal:* the sum of the minimum of every subarray, modulo 1,000,000,007.
*How:* section 6. First write the O(n^2) version and use it as the test. Then change one side to the other strictness and confirm that the answer does not change; make both strict and watch it fail on `[2, 2]`.

**Problem 8. LeetCode 402, Remove K Digits.**
*Where:* LeetCode (Medium on the mirror site).
*Goal:* the smallest number after removing `k` digits.
*How:* section 7. Test the edge cases: `"10200"` with 1, `"10"` with 2, a string that is already non-decreasing (`"112"` with 1), and `k` = 0.

**More practice.**

- **LeetCode 853, Car Fleet** (Medium on the mirror site). Sort the cars by position; the time each needs to reach the target is a number, and a car that would arrive no later than the one ahead joins its fleet. What does the stack hold?
- **LeetCode 456, 132 Pattern** (Medium on the mirror site). Scan from the right; keep a stack of candidates for the "3" and remember the largest value popped from it as the "2". A value below that "2" completes the pattern.
- **LeetCode 85, Maximal Rectangle** (Hard on the mirror site). For each row, build the heights of the runs of 1s that end at that row, then run problem 5 on every row.
- **LeetCode 316, Remove Duplicate Letters** (Medium on the mirror site). The greedy stack of section 7, with two additions: a letter already on the stack is skipped, and a letter may be popped only if it appears again later.

---

## Answers

**Problem A.** The stack as index:value after each `i`:

| i | `a[i]` | indexes answered (their answer is `i`) | stack after, as index:value |
|---|---|---|---|
| 0 | 5 | none | [0:5] |
| 1 | 3 | none | [0:5, 1:3] |
| 2 | 8 | 1, 0 | [2:8] |
| 3 | 3 | none | [2:8, 3:3] |
| 4 | 6 | 3 | [2:8, 4:6] |
| 5 | 2 | none | [2:8, 4:6, 5:2] |
| 6 | 7 | 5, 4 | [2:8, 6:7] |

The answer vector is `[2, 2, 7, 4, 6, 6, 7]`, with 7 meaning "none". The indexes 2 and 6 (the values 8 and 7) are still on the stack at the end, so nothing is greater than them to their right. In values: `[8, 8, none, 6, 7, 7, none]`.

**Problem B.**

| i | bar | pops: height, left wall, width, area | best | stack after, as index:height |
|---|---|---|---|---|
| 0 | 3 | none | 0 | [0:3] |
| 1 | 1 | h=3, left=-1, width=1, area=3 | 3 | [1:1] |
| 2 | 3 | none | 3 | [1:1, 2:3] |
| 3 | 2 | h=3, left=1, width=1, area=3 | 3 | [1:1, 3:2] |
| 4 | 2 | h=2, left=1, width=2, area=4 | 4 | [1:1, 4:2] |
| 5 | 0 (end) | h=2, left=1, width=3, area=6; h=1, left=-1, width=5, area=5 | 6 | [] |

The best rectangle, area 6, is measured by the *last* of the two equal 2s: its left wall is the 1 at index 1, so its width is 3 (bars 2 to 4). The first 2 (index 3) is popped by the second at `i = 4` and measures a width of 2 and an area of 4: a real rectangle, but not the largest one of that height. No harm is done, because the bar that continues the run measures the whole stretch.

**Problem C.**

| i | bottom (index:height) | left wall (index:height) | width | depth | layer | total so far |
|---|---|---|---|---|---|---|
| 2 | 1:0 | 0:3 | 1 | 2 | 2 | 2 |
| 4 | 3:0 | 2:2 | 1 | 2 | 2 | 4 |
| 4 | 2:2 | 0:3 | 3 | 1 | 3 | 7 |

Total 7. Column by column: the bars at indexes 1, 2 and 3 hold 3, 1 and 3 units (the tallest bar on each side is 3 on the left and 4 on the right), which is 7 as well. The two 2-unit layers are the pits at indexes 1 and 3; the 3-unit layer lies above the bar at index 2, between the walls 3 and 4.

**Problem D.**

| i | `a[i]` | prev | next | count | contribution |
|---|---|---|---|---|---|
| 0 | 11 | -1 | 4 | 4 | 44 |
| 1 | 81 | 0 | 3 | 2 | 162 |
| 2 | 94 | 1 | 3 | 1 | 94 |
| 3 | 43 | 0 | 4 | 3 | 129 |
| 4 | 3 | -1 | 5 | 5 | 15 |

The sum is 444. Element 43 (index 3) has `prev` = 0 and `next` = 4, so the subarrays are those that start at index 1, 2 or 3 and end at index 3: `[81, 94, 43]`, `[94, 43]` and `[43]`. That is a count of 3, and the contribution is 129. Element 3 at index 4 is the minimum of every subarray that ends at index 4, so its count is 5.

**Problem E.** (a) The prices `5, 5`: the right spans are 1 and 2, but with `<` the second 5 does not pop the first, and the answers are 1 and 1. (b) The result is still right. With `>` equal bars stay on the stack, so a popped bar can have an equal bar as its left wall and measure a too-narrow rectangle; but the *first* bar of a run of equals has the smaller bar (or the edge) as its left wall and measures the full width. For `[2, 2, 2]` all three are popped at the end by the sentinel and measure widths 1, 2 and 3, areas 2, 4 and 6. Both rules return the maximum; they differ in which bar finds it. (c) With both sides strict `[2, 2]` gives 8: each 2 claims the whole subarray `[2, 2]`. With both non-strict it gives 4, because neither does. The right answer is 6.

**Problem 1.** The loop of section 2 runs over `nums2` once and costs at most 2m comparisons for `m` = `nums2.size()`; the lookups are one map access for each of the `n` numbers of `nums1`. In all that is O(m + n) on average with a hash map.

**Problem 2.** The variant that pushes on both laps returns the same answers (every index pushed on the second lap looks at the same elements that the first lap already used for it) but can reach a stack of up to 2n entries (when all the elements are equal) and makes more comparisons (try an increasing array).

**Problem 3.** The jump version goes from the right: for day `i`, start at `j = i + 1` and, while `t[j] <= t[i]`, jump `j += wait[j]` (if `wait[j]` is 0 there is no warmer day at all). The chain of jumps that starts at day `i + 1` visits exactly the days that the stack would hold at that moment, and the days it passes over are the ones the stack would pop. So it is the same algorithm in disguise and stays linear: the checks counted at most 2n steps on every test input.

**Problem 4.** The brute force walks back through the stored prices while they are at most today's. The stack and the brute force must agree on runs of equal prices; the `<=` in the pop is what makes them agree.

**Problem 5.** The staircase `0, 1, ..., 99999` has its best rectangle at height 50,000, width 50,000: an area of 2,500,000,000, which an `int` cannot hold (the largest is 2,147,483,647).

**Problem 6.** The stack version is O(n) in time and memory; the two-pointer version is O(n) in time and O(1) in memory. They agree on every input; use the pointers when memory matters, and the stack when you want to see the pits.

**Problem 7.** Switching to "previous strictly smaller, next smaller or equal" gives the same sum (each subarray goes to its rightmost minimum instead of its leftmost). Making both strict gives 8 on `[2, 2]`, not 6.

**Problem 8.** `"10200"` with 1: the 0 pops the 1, the stack ends `0200`, stripping gives `200`. `"10"` with 2: both digits go, so `"0"`. `"112"` with 1: nothing pops, the leftover removal comes off the end, giving `"11"`. With `k` = 0 the string comes back as it is (after stripping leading zeros).

---

## Cheat sheet

- **Waiting room:** a stack of indexes without an answer, sorted by value; a new element pops everyone it answers, then joins. Each index is pushed once and popped at most once, so a pass is at most 2n comparisons.
- **Four rules:** pop while `top < x` (next greater), `top <= x` (next greater or equal), `top > x` (next smaller), `top >= x` (next smaller or equal). The index left under `x` is the previous one on the other side, with the opposite strictness.
- **Indexes or pairs:** indexes for distances and widths; `(value, count)` pairs for streams (stock span).
- **Circular:** two laps, push on the first.
- **Histogram:** sentinel bar of height 0 at the end; pop while `h[top] >= cur`; width is `i - left - 1` with `left` the new top or `-1`; area in `long long`.
- **Rain water:** pop while `h[top] < h[i]`; layer is `(i - left - 1) * (min(h[left], h[i]) - h[bottom])`; no left wall means no water.
- **Contribution counting:** count is `(i - prev) * (next - i)`; exactly one side strict.
- **Remove K digits:** pop while `k > 0` and `top > c`; cut leftovers from the end; strip leading zeros; empty means `"0"`.
- **Not for windows:** a window of size k needs the deque of tutorial 03.

---

## Sources

- Problem statements and labels, from mirror sites because LeetCode itself could not be opened from here: [496](https://leetcode.doocs.org/en/lc/496/) (Easy; `nums1` within `nums2`, values unique, sizes up to 1000), [503](https://leetcode.doocs.org/en/lc/503/) (Medium; length up to 10,000), [739](https://leetcode.doocs.org/en/lc/739/) (no label on any page I read; length up to 30,000, temperatures 30 to 100), [901](https://brain-leetcode.pages.dev/09xx/901/) (Medium; at most 10,000 calls), [84](https://github.com/kyle8998/Practice-Coding-Questions/blob/master/leetcode/84-Hard-Largest-Rectangle-In-Histogram/problem.md) (Hard; no limits shown), [42](https://github.com/kushalvyas/leetcode/blob/master/problems/42.trapping-rain-water.md) (Hard; no limits shown), [907](https://brain-leetcode.pages.dev/09xx/907/) (Medium; n and values up to 30,000, modulo 1,000,000,007), [402](https://brain-leetcode.pages.dev/04xx/402/) (Medium; length up to 100,000), [853](https://lyte-code.pages.dev/problems/car-fleet) (Medium), [456](https://leetcode.doocs.org/lc/456/) (Medium), [85](https://brain-leetcode.pages.dev/00xx/85/) (Hard) and [316](https://ycj.codeberg.page/problems/0316-remove-duplicate-letters) (Medium).
- The comparison counts of section 1.1 come from the tutorial's own test program (counting wrapper type, n = 20,000, seed 5); they are exact and machine independent.
- Sedgewick and Wayne, *Algorithms*, section 1.3, for the stack; the monotonic stack itself is folklore in competitive programming, and the name of the general problem, all nearest smaller values, is the search term for the literature.

---

**Next:** tutorial 03, the monotonic deque (sliding window maximum and minimum, shortest subarray with sum at least K, the window with a limit, jump game VI). Say "next" when you are ready.
