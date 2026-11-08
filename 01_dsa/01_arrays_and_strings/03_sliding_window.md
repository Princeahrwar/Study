# DSA 01.03: Sliding Window

*Part of the [Arrays and Strings plan](00_plan.md) · Time: about 4 hours (1 reading, 3 problems) · You need: tutorials [01](01_complexity_and_arrays_in_memory.md) and [02](02_two_pointers.md) · Companion code: [`03_sliding_window_demo.cpp`](03_sliding_window_demo.cpp)*

By the end of this tutorial you can:

- recognise "longest / shortest / count of subarrays or substrings with a property" as a sliding-window problem;
- write the fixed-size window and both variable-size templates (longest and shortest) from memory;
- count subarrays with the "at most K" trick;
- explain when a sliding window does **not** work, and what to use instead.

---

## 1. The idea

A **window** is a contiguous range `a[l..r]` of an array or string. You track a summary of what is inside it (a sum, a count of each character, a number of distinct values) and move the window along the data.

```text
 index:   0   1   2   3   4   5   6
 a:       2   1   5   1   3   2   7
              [l ........ r]            window = a[1..4],  length = r - l + 1 = 4
```

The naive way recomputes the summary for every window: O(n × window length). The sliding window **updates** the summary instead: when `r` moves right, add the new element; when `l` moves right, remove the old one. Each update is O(1), and each pointer only moves forward, so the whole pass is **O(n)**.

It is the two-pointer idea from tutorial 02, with one difference: the two pointers define a **range** whose contents you keep summarised.

There are two kinds of window:

| Kind | Window size | Example question |
|---|---|---|
| **Fixed** | always k | the largest sum of any k consecutive numbers |
| **Variable** | grows and shrinks | the longest substring with no repeated character |

---

## 2. Fixed-size window

*Question:* find the largest sum of any `k` consecutive elements.

*Brute force:* for each start, add up `k` numbers: O(n × k).

*Window:* build the first window, then slide. Each slide adds the element entering on the right and removes the element leaving on the left.

```cpp
// Largest sum of any k consecutive elements (requires 1 <= k <= a.size()).
long long maxSumOfK(const std::vector<int>& a, int k) {
    long long window = 0;
    for (int i = 0; i < k; ++i) window += a[i];      // the first window
    long long best = window;
    for (int r = k; r < (int)a.size(); ++r) {
        window += a[r];                              // slide: add the new element ...
        window -= a[r - k];                          // ... and drop the oldest one
        best = std::max(best, window);
    }
    return best;
}
```

Trace on `{2, 1, 5, 1, 3, 2}` with `k = 3`:

| Window | Sum | How it was computed |
|---|---|---|
| `[2, 1, 5]` | 8 | first window |
| `[1, 5, 1]` | 7 | 8 + 1 − 2 |
| `[5, 1, 3]` | 9 | 7 + 3 − 1 |
| `[1, 3, 2]` | 6 | 9 + 2 − 5 |

The answer is 9. O(n) time, O(1) space. Note the rule: the element leaving the window at step `r` is `a[r - k]`.

**The summary can be a data structure.** Next question: how many windows of size `k` have `k` *different* values? The summary is now a frequency map, and sliding means "add one, remove one":

```cpp
// How many windows of size k have k different values?
int countDistinctWindows(const std::vector<int>& a, int k) {
    std::unordered_map<int, int> freq;               // value -> how many times it is in the window
    int count = 0;
    for (int r = 0; r < (int)a.size(); ++r) {
        ++freq[a[r]];                                // add the new element
        if (r >= k) {                                // the window now has k+1 elements: drop the oldest
            if (--freq[a[r - k]] == 0) freq.erase(a[r - k]);
        }
        if (r >= k - 1 && (int)freq.size() == k) ++count;
    }
    return count;
}
```

For `{1, 2, 1, 3, 4}` and `k = 3`: `[1,2,1]` has two different values (no), `[2,1,3]` and `[1,3,4]` have three (yes). The answer is 2. Notice that we **erase a key when its count reaches zero**. If we left zero-count entries in the map, `freq.size()` would count values that are no longer in the window.

---

## 3. Variable-size window

Now the window size is not given. You grow it on the right, and shrink it from the left whenever a rule says so. There are two templates, depending on whether you want the **longest** or the **shortest** window.

### 3.1 Longest window that stays valid

The rule: the window is *valid* if it satisfies some condition (sum at most a limit, no repeated character, and so on). Grow `r` one step at a time. If the window becomes invalid, shrink from the left **until it is valid again**. At that point the window is the longest valid one ending at `r`, so record it.

```text
for r = 0 .. n-1:
    add a[r] to the window summary
    while the window is invalid:
        remove a[l] from the summary;  l++
    best = max(best, r - l + 1)          <- record AFTER shrinking
```

Example: for non-negative numbers, the length of the longest subarray whose sum is at most `limit`.

```cpp
// Non-negative numbers: length of the longest subarray whose sum is at most limit (limit >= 0).
int longestSumAtMost(const std::vector<int>& a, long long limit) {
    long long sum = 0;                               // window state: the sum of a[l..r]
    int best = 0, l = 0;
    for (int r = 0; r < (int)a.size(); ++r) {
        sum += a[r];                                 // grow: take a[r] into the window
        while (sum > limit) sum -= a[l++];           // invalid: shrink from the left until valid again
        best = std::max(best, r - l + 1);            // the window is valid here: record its length
    }
    return best;
}
```

Trace on `{2, 1, 3, 1, 1, 5}` with `limit = 5`:

| r | add | sum before shrinking | shrink | window `[l..r]` | length |
|---|---|---|---|---|---|
| 0 | 2 | 2 | | [0..0] | 1 |
| 1 | 1 | 3 | | [0..1] | 2 |
| 2 | 3 | 6 (too big) | drop 2, sum 4 | [1..2] | 2 |
| 3 | 1 | 5 | | [1..3] | **3** |
| 4 | 1 | 6 (too big) | drop 1, sum 5 | [2..4] | 3 |
| 5 | 5 | 10 (too big) | drop 3, 1, 1, sum 5 | [5..5] | 1 |

The answer is 3.

### 3.2 Shortest window that becomes valid

Here the logic is reversed: a window is valid once it has *enough* (a big enough sum, all the required characters). Grow `r` until the window is valid. Then, **while it is valid**, record it and shrink from the left to see if a shorter valid window exists.

```text
for r = 0 .. n-1:
    add a[r] to the window summary
    while the window is valid:
        best = min(best, r - l + 1)      <- record INSIDE the loop
        remove a[l] from the summary;  l++
```

The difference between the two templates is the single most important detail of this tutorial:

| | Longest | Shortest |
|---|---|---|
| Shrink while the window is | **invalid** | **valid** |
| Record the answer | after the `while` | inside the `while` |

Example: the length of the shortest substring that contains at least one `a`, one `b` and one `c`.

```cpp
// Length of the shortest substring of s that contains at least one 'a', one 'b' and one 'c' (0 if none).
int shortestWithABC(const std::string& s) {
    int count[3] = {0, 0, 0};
    int have = 0;                                    // how many of a, b, c are in the window
    int best = INT_MAX, l = 0;
    for (int r = 0; r < (int)s.size(); ++r) {
        if (s[r] >= 'a' && s[r] <= 'c' && ++count[s[r] - 'a'] == 1) ++have;
        while (have == 3) {                          // valid: record it, then shrink
            best = std::min(best, r - l + 1);
            if (s[l] >= 'a' && s[l] <= 'c' && --count[s[l] - 'a'] == 0) --have;
            ++l;
        }
    }
    return best == INT_MAX ? 0 : best;
}
```

Trace on `"abcbca"`:

| r | added | what happens | window |
|---|---|---|---|
| 0 | a | have = 1 | `a` |
| 1 | b | have = 2 | `ab` |
| 2 | c | have = 3, valid, length 3 → **best = 3**. Drop `a`: have = 2 | `bc` |
| 3 | b | have = 2 | `bcb` |
| 4 | c | have = 2 | `bcbc` |
| 5 | a | have = 3, valid, length 5. Drop `b` (another `b` remains): still valid, length 4. Drop `c` (another remains): still valid, length 3. Drop `b`: have = 2 | `ca` |

The answer is 3. Notice that `have` changes only when a count goes from 0 to 1 or from 1 to 0. Tracking "how many requirements are met" is what keeps the validity check O(1).

### 3.3 Why the nested loop is still O(n)

There is a `while` inside the `for`, which looks like O(n²). It is not. `r` moves forward n times, and `l` also only moves forward, never more than n times in total. Each element **enters the window once and leaves once**, so the total work is at most 2n steps. This is the amortised argument from tutorial 01, section 1.7: one step may do a lot of shrinking, but the average is O(1).

### 3.4 Counting windows: the "at most K" trick

*Question:* how many subarrays have **exactly** K different values? Counting "exactly" directly is awkward, because shrinking can overshoot. The trick is a difference:

> exactly K = (at most K) − (at most K − 1)

And "at most K" is easy: it is a *longest valid window* problem, with one extra idea. For each `r`, after shrinking, **every** subarray that ends at `r` and starts anywhere in `[l, r]` is valid (a shorter subarray cannot have more different values). That is `r - l + 1` subarrays to add up.

```cpp
// Number of subarrays with at most k distinct values.
long long atMostKDistinct(const std::vector<int>& a, int k) {
    std::unordered_map<int, int> freq;
    long long count = 0;
    int l = 0;
    for (int r = 0; r < (int)a.size(); ++r) {
        ++freq[a[r]];
        while ((int)freq.size() > k) {               // too many different values: shrink
            if (--freq[a[l]] == 0) freq.erase(a[l]);
            ++l;
        }
        count += r - l + 1;                          // every subarray that ends at r and starts in [l, r] is valid
    }
    return count;
}

// Number of subarrays with exactly k distinct values.
long long exactlyKDistinct(const std::vector<int>& a, int k) {
    return atMostKDistinct(a, k) - atMostKDistinct(a, k - 1);
}
```

On `{1, 2, 1, 2, 3}` with `k = 2`: the "at most 2" counts added at each `r` are 1, 2, 3, 4 and then 2 (at `r = 4` the value 3 forces `l` forward to index 3), a total of **12**. "At most 1" is **5** (only single elements). So exactly 2 different values: 12 − 5 = **7**.

The same "difference of two counts" idea works for other "exactly" questions, such as subarrays with exactly K odd numbers.

---

## 4. When a sliding window does not work

The shrink step assumes **dropping elements from the left can only help** (the sum gets smaller, the number of distinct values shrinks). That is true for non-negative numbers, but it is false when numbers can be **negative**: dropping a negative number makes the sum *bigger*, so a window you shrank past might have been the right one.

Example. Shortest subarray with sum at least 167, in `{84, -37, 32, 40, 95}`. The right answer is 3 (`32 + 40 + 95 = 167`). The shortest-window template finds only the whole array (length 5): at `r = 4` the sum is 214, it drops 84 and the sum falls to 130, which is below 167, so it stops. It never considers starting at `-37` and dropping it too.

| Situation | Use |
|---|---|
| Non-negative numbers, or a property that gets monotonically better as you shrink | sliding window |
| Numbers may be negative, "subarray sum equals K" | prefix sums and a hash map (tutorial 04) |
| Shortest subarray with sum at least K, negatives allowed | prefix sums with a monotonic deque (later, in Stacks and Queues) |
| Maximum of every window of size k | monotonic deque (later, in Stacks and Queues) |

Before you reach for a window, ask: **"if I shrink from the left, does the window only get better?"** If the answer is not clearly yes, it is not a window problem.

---

## 5. What to keep as the window summary

| Question type | Summary | Notes |
|---|---|---|
| Sum or average | one number | use `long long` |
| Characters | `int count[26]`, `count[128]` or `count[256]` | an array is faster than a map when the alphabet is small |
| Any values | `unordered_map<value, count>` | erase a key when its count reaches 0 |
| "Does it contain all of t?" | `need[]` counts and a `missing` counter | O(1) validity check (Problem 5) |
| Max or min in the window | a monotonic deque | later |

The check "is the window valid?" must be **O(1)**. If you have to scan the whole summary each time, the pass becomes slower (26 × n is still fine, but not n × n).

---

## 6. Mistakes that cost points

1. **Wrong template.** For *longest*, shrink while *invalid* and record after the loop. For *shortest*, shrink while *valid* and record inside the loop. Mixing them up gives wrong answers on easy tests.
2. **`if` instead of `while` when shrinking.** One removal is often not enough.
3. **Forgetting to remove `a[l]` from the summary** (or removing it after `l++`, which removes the wrong element).
4. **Window length.** It is `r - l + 1`, not `r - l`.
5. **Zero-count entries left in a map**, which makes `size()` wrong (section 2).
6. **Using a window when the data has negative numbers** (section 4).
7. **Overflow.** Sums of many ints need `long long`.
8. **Empty or tiny inputs.** `k > n`, `k = 0`, empty string. Decide what the answer is and test it first.

---

## 7. Questions you will be asked

**How do you recognise a sliding-window problem?** A contiguous subarray or substring, with a "longest", "shortest" or "count" question about a property that gets better or worse monotonically as the window grows.

**Why is it O(n) when there is a loop inside a loop?** Each element enters and leaves the window at most once, so the two pointers move at most 2n steps in total.

**Longest versus shortest: what changes?** Shrink while invalid and record after the loop, versus shrink while valid and record inside the loop.

**How do you count subarrays with exactly K of something?** Count "at most K" and subtract "at most K − 1".

**Why does it fail with negative numbers?** Dropping a negative element from the left increases the sum, so shrinking is no longer safe. Use prefix sums and a hash map, or a monotonic deque.

**How would you do it on a stream that does not fit in memory?** A sliding window is a natural fit: it only needs the window contents (or just its summary), not the whole input.

**Fixed window of size k, with the maximum of each window?** The summary needs a maximum, which a plain counter cannot give while elements leave; use a monotonic deque (covered later).

---

## 8. Problems

**How to use this section.** Try each problem for 15 minutes (easy) or 30 minutes (medium or hard) before reading anything. If you are stuck, read only the **How** lines, then try again, and only then open the answer at the bottom. Log each attempt in the [practice log](../practice_log.md). Ask yourself first: fixed or variable? Longest or shortest or count? What is the summary?

### On paper

**Problem A: Trace it.** Run `longestSumAtMost` by hand on `{3, 1, 2, 7, 4, 2, 1, 1, 5}` with `limit = 8`. Write a table like the one in section 3.1. What is the answer?
*Where:* paper. *How:* add `a[r]`, then shrink while the sum is above 8, and record the length.

**Problem B: Pick the tool.** For each, say whether a sliding window works, and if not, what you would use.
(1) Longest substring with at most 2 distinct characters.
(2) Number of subarrays with sum exactly 10 in an array that may contain negative numbers.
(3) Shortest subarray with sum at least 10 in an array of positive numbers.
(4) The maximum of every window of size 3.
*Where:* paper. *How:* use the question at the end of section 4.

### In code

**Problem 1: Maximum Average Subarray I** (LeetCode 643, easy)
*Where:* leetcode.com/problems/maximum-average-subarray-i. *Goal:* the largest average of any `k` consecutive elements.
*How:* the fixed window from section 2. Compute the best sum, then divide by `k` once at the end.

**Problem 2: Longest Substring Without Repeating Characters** (LeetCode 3, medium)
*Where:* leetcode.com/problems/longest-substring-without-repeating-characters. *Goal:* the length of the longest substring in which no character repeats.
*How:* the "longest" template. The summary is a count per character. The window is invalid when the character you just added now appears twice, so shrink until it appears once.

**Problem 3: Minimum Size Subarray Sum** (LeetCode 209, medium)
*Where:* leetcode.com/problems/minimum-size-subarray-sum. *Goal:* the length of the shortest subarray whose sum is at least `target` (all numbers are positive). Return 0 if none.
*How:* the "shortest" template with the sum as the summary. Record inside the loop.

**Problem 4: Permutation in String** (LeetCode 567, medium)
*Where:* leetcode.com/problems/permutation-in-string. *Goal:* does `s2` contain a substring that is a rearrangement of `s1`?
*How:* a fixed window of size `len(s1)`. Keep a count array for `s1` and one for the window, and compare them each step (26 numbers).

**Problem 5: Longest Repeating Character Replacement** (LeetCode 424, medium)
*Where:* leetcode.com/problems/longest-repeating-character-replacement. *Goal:* you may replace at most `k` characters. What is the longest substring of one repeated letter you can get?
*How:* the "longest" template. A window is valid if `length − (count of its most frequent letter) ≤ k`: the other letters are the ones you would replace.

**Problem 6: Minimum Window Substring** (LeetCode 76, hard; try it last)
*Where:* leetcode.com/problems/minimum-window-substring. *Goal:* the shortest substring of `s` that contains every character of `t`, including repeats.
*How:* this is `shortestWithABC` with arbitrary characters and counts. Keep `need[c]` (how many of each character `t` still requires) and a single number `missing` (how many required characters the window still lacks). The window is valid exactly when `missing == 0`.

---

## Answers

Try before reading. Code is written as plain functions; LeetCode wraps it in `class Solution { public: ... };`.

### Problem A

| r | add | sum | shrink | window | length |
|---|---|---|---|---|---|
| 0 | 3 | 3 | | [0..0] | 1 |
| 1 | 1 | 4 | | [0..1] | 2 |
| 2 | 2 | 6 | | [0..2] | 3 |
| 3 | 7 | 13 | drop 3 → 10, drop 1 → 9, drop 2 → 7 | [3..3] | 1 |
| 4 | 4 | 11 | drop 7 → 4 | [4..4] | 1 |
| 5 | 2 | 6 | | [4..5] | 2 |
| 6 | 1 | 7 | | [4..6] | 3 |
| 7 | 1 | 8 | | [4..7] | **4** |
| 8 | 5 | 13 | drop 4 → 9, drop 2 → 7 | [6..8] | 3 |

The answer is **4**, the window `4, 2, 1, 1` (sum 8).

### Problem B

1. **Yes.** Longest window, summary is a count per character, invalid when more than 2 distinct.
2. **No.** Negative numbers break the shrinking rule. Use prefix sums and a hash map (tutorial 04).
3. **Yes.** Shortest window, all numbers positive: Problem 3.
4. **Not directly.** A counter cannot give the maximum while elements leave. Use a monotonic deque (later).

### Problem 1: Maximum Average Subarray I

```cpp
double findMaxAverage(const std::vector<int>& nums, int k) {
    long long window = 0;
    for (int i = 0; i < k; ++i) window += nums[i];
    long long best = window;
    for (int r = k; r < (int)nums.size(); ++r) {
        window += nums[r];
        window -= nums[r - k];
        best = std::max(best, window);
    }
    return (double)best / k;
}
```

Compare sums, not averages, and divide once at the end. O(n) time, O(1) space.

### Problem 2: Longest Substring Without Repeating Characters

```cpp
int lengthOfLongestSubstring(const std::string& s) {
    int count[256] = {0};                            // how many times each character is in the window
    int best = 0, l = 0;
    for (int r = 0; r < (int)s.size(); ++r) {
        ++count[(unsigned char)s[r]];
        while (count[(unsigned char)s[r]] > 1)       // s[r] now appears twice: shrink from the left
            --count[(unsigned char)s[l++]];
        best = std::max(best, r - l + 1);
    }
    return best;
}
```

O(n) time, O(1) space (256 counters). A faster variant stores the last index of each character and jumps `l` straight past the previous occurrence, but the version above is the one to write first.

### Problem 3: Minimum Size Subarray Sum

```cpp
int minSubArrayLen(int target, const std::vector<int>& nums) {
    int best = INT_MAX, l = 0;
    long long sum = 0;
    for (int r = 0; r < (int)nums.size(); ++r) {
        sum += nums[r];
        while (sum >= target) {                      // valid: record it, then try to shrink
            best = std::min(best, r - l + 1);
            sum -= nums[l++];
        }
    }
    return best == INT_MAX ? 0 : best;
}
```

O(n) time, O(1) space. Returns 0 when no window reaches the target.

### Problem 4: Permutation in String

```cpp
bool checkInclusion(const std::string& s1, const std::string& s2) {
    int k = (int)s1.size();
    if (k == 0 || k > (int)s2.size()) return k == 0;
    int need[26] = {0}, have[26] = {0};
    for (char c : s1) ++need[c - 'a'];
    for (int r = 0; r < (int)s2.size(); ++r) {
        ++have[s2[r] - 'a'];
        if (r >= k) --have[s2[r - k] - 'a'];         // keep the window at exactly k characters
        if (r >= k - 1 && std::equal(need, need + 26, have)) return true;
    }
    return false;
}
```

Two strings are rearrangements of each other exactly when their letter counts match. O(26 × n) time, O(1) space.

### Problem 5: Longest Repeating Character Replacement

```cpp
int characterReplacement(const std::string& s, int k) {
    int count[26] = {0};
    int best = 0, l = 0;
    auto maxFreq = [&] { return *std::max_element(count, count + 26); };
    for (int r = 0; r < (int)s.size(); ++r) {
        ++count[s[r] - 'A'];
        while ((r - l + 1) - maxFreq() > k)          // more than k characters would need replacing: shrink
            --count[s[l++] - 'A'];
        best = std::max(best, r - l + 1);
    }
    return best;
}
```

O(26 × n) time, O(1) space. For `"AABABBA"` with `k = 1` the answer is 4 (`AABA` becomes `AAAA`). A known optimisation never recomputes the maximum (it only needs the best value ever seen), which gives true O(n), but the clear version above is correct and fast enough.

### Problem 6: Minimum Window Substring

```cpp
std::string minWindow(const std::string& s, const std::string& t) {
    if (t.empty()) return "";
    int need[256] = {0};
    for (char c : t) ++need[(unsigned char)c];
    int missing = (int)t.size();                     // how many characters of t the window still lacks
    int bestStart = 0, bestLen = INT_MAX, l = 0;
    for (int r = 0; r < (int)s.size(); ++r) {
        if (need[(unsigned char)s[r]]-- > 0) --missing;          // this character was still needed
        while (missing == 0) {                                   // the window covers t: record it, then shrink
            if (r - l + 1 < bestLen) { bestLen = r - l + 1; bestStart = l; }
            if (++need[(unsigned char)s[l++]] > 0) ++missing;    // we dropped a character that was needed
        }
    }
    return bestLen == INT_MAX ? "" : s.substr(bestStart, bestLen);
}
```

`need[c]` is how many more copies of `c` the window needs. It goes **negative** when the window has surplus copies, which is how we know a character can be dropped without breaking validity: after `++need[...]` it is only `> 0` if the character was truly required. For `s = "ADOBECODEBANC"`, `t = "ABC"` the answer is `"BANC"`. O(|s| + |t|) time, O(1) space.

---

## Cheat sheet

- A window is a range `a[l..r]` with a summary you update in O(1): add on the right, remove on the left. Whole pass: O(n).
- **Fixed size k:** the element leaving at step `r` is `a[r - k]`.
- **Longest valid:** shrink while *invalid*, record *after* the loop.
- **Shortest valid:** shrink while *valid*, record *inside* the loop.
- **Count subarrays:** after shrinking, add `r - l + 1`. "Exactly K" = at most K minus at most K − 1.
- The validity check must be O(1): track counts and a "how many requirements are met" number.
- Erase map keys when their count hits zero.
- Negative numbers break the shrinking rule: use prefix sums (tutorial 04).
- Ask before coding: fixed or variable? longest, shortest or count? what is the summary?

---

## Sources

- Counts, outputs and timings in this tutorial come from the companion `03_sliding_window_demo.cpp` (it prints the number of checks it passed) on the machine described in the text. Times are that machine's, not yours.
- The algorithms and data structures are standard; for further reading see the "Extra practice and resources" section of the [area plan](00_plan.md), which lists the textbook chapters and free references for this area. The explanations and code are my own.
- LeetCode numbers and titles are from memory; check a number before you rely on it.

**Next:** tutorial 04, Prefix Sums and Difference Arrays. Say "next" when you are ready.
